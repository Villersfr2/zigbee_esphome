#include "power_management.h"

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "ezbee/nwk.h"
#include <stdio.h>
#endif

namespace esphome {
namespace power_management {

static const char *const TAG = "power_management";

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
static volatile uint64_t s_sleep_total_us = 0;
static volatile uint32_t s_sleep_entries = 0;
static volatile int64_t s_last_sleep_us = 0;

static esp_err_t pm_sleep_enter_cb(int64_t expected_sleep_time_us, void *arg) {
  (void) expected_sleep_time_us;
  (void) arg;
  return ESP_OK;
}

static esp_err_t pm_sleep_exit_cb(int64_t actual_sleep_time_us, void *arg) {
  (void) arg;
  if (actual_sleep_time_us > 0) {
    s_sleep_total_us += static_cast<uint64_t>(actual_sleep_time_us);
    s_last_sleep_us = actual_sleep_time_us;
    s_sleep_entries++;
  }
  return ESP_OK;
}
#endif

static void dump_zigbee_sleep_state_() {
  const bool rx_on = ezb_nwk_get_rx_on_when_idle();
  const ezb_shortaddr_t short_addr = ezb_nwk_get_short_address();
  const bool joined = short_addr != EZB_NWK_ADDR_UNKNOWN;
  ESP_LOGW(TAG, "[ZIGBEE SLEEP] joined=%s | short_addr=0x%04x | sleepy=%s | rx_on_when_idle=%s",
           YESNO(joined), short_addr, YESNO(!rx_on), rx_on ? "TRUE" : "FALSE");
}

static void enforce_zigbee_sleepy_() {
  const ezb_shortaddr_t short_addr = ezb_nwk_get_short_address();
  const bool joined = short_addr != EZB_NWK_ADDR_UNKNOWN;
  const bool rx_on = ezb_nwk_get_rx_on_when_idle();
  if (joined && rx_on) {
    ESP_LOGW(TAG, "[ZIGBEE SLEEP] Stack restored rx_on_when_idle=TRUE after startup; forcing FALSE for sleepy end device");
    ezb_nwk_set_rx_on_when_idle(false);
    ESP_LOGI(TAG, "[ZIGBEE SLEEP] rx_on_when_idle is now %s", ezb_nwk_get_rx_on_when_idle() ? "TRUE" : "FALSE");
  }
}

void PowerManagementComponent::setup() {
  if (!this->enable_light_sleep_) {
    ESP_LOGI(TAG, "Automatic light sleep disabled");
    return;
  }
  this->set_timeout(this->start_delay_ms_, [this]() { this->configure_pm_(); });
}

void PowerManagementComponent::configure_pm_() {
#ifdef CONFIG_PM_ENABLE
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
  // The Zigbee stack can restore RxOnWhenIdle from persisted network state after
  // ZigBeeComponent::setup() initially configured it. Re-apply sleepy behavior
  // after the network has had time to restore/join, immediately before PM starts.
  enforce_zigbee_sleepy_();

  const int cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  esp_pm_config_t pm_config = {
      .max_freq_mhz = cpu_freq_mhz,
      .min_freq_mhz = cpu_freq_mhz,
      .light_sleep_enable = true,
  };

  esp_err_t err = esp_pm_configure(&pm_config);
  if (err == ESP_OK) {
    this->pm_configured_ = true;
    this->last_debug_ms_ = millis();
    this->last_awake_us_ = esp_timer_get_time();
    ESP_LOGI(TAG, "Automatic light sleep enabled at %d MHz (start delay: %lu ms)", cpu_freq_mhz,
             (unsigned long) this->start_delay_ms_);
    if (this->power_down_peripherals_) {
      ESP_LOGI(TAG, "Peripheral clocks/power domains are managed automatically by ESP-IDF light sleep");
    }
    if (this->sleep_debug_) {
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
      esp_pm_sleep_cbs_register_config_t cbs = {};
      cbs.enter_cb = pm_sleep_enter_cb;
      cbs.exit_cb = pm_sleep_exit_cb;
      cbs.enter_cb_user_arg = this;
      cbs.exit_cb_user_arg = this;
      cbs.enter_cb_prior = 0;
      cbs.exit_cb_prior = 0;
      esp_err_t cb_err = esp_pm_light_sleep_register_cbs(&cbs);
      if (cb_err == ESP_OK) {
        ESP_LOGI(TAG, "Real light-sleep accounting enabled (ESP-IDF callbacks), report every %lu ms",
                 (unsigned long) this->sleep_debug_interval_ms_);
      } else {
        ESP_LOGE(TAG, "Could not register light-sleep callbacks: %s", esp_err_to_name(cb_err));
      }
#else
      ESP_LOGW(TAG, "Sleep debug requested but CONFIG_PM_LIGHT_SLEEP_CALLBACKS is disabled");
#endif
      dump_zigbee_sleep_state_();
      ESP_LOGW(TAG, "[PM LOCKS] Initial power-management lock dump follows:");
      esp_pm_dump_locks(stdout);
    }
  } else {
    ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
    this->mark_failed();
  }
#else
  ESP_LOGE(TAG, "CONFIG_FREERTOS_USE_TICKLESS_IDLE is not enabled");
  this->mark_failed();
#endif
#else
  ESP_LOGE(TAG, "CONFIG_PM_ENABLE is not enabled");
  this->mark_failed();
#endif
}

void PowerManagementComponent::loop() {
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_LIGHT_SLEEP_CALLBACKS
  if (!this->pm_configured_ || !this->sleep_debug_)
    return;

  const uint32_t now_ms = millis();
  if ((uint32_t) (now_ms - this->last_debug_ms_) < this->sleep_debug_interval_ms_)
    return;

  const uint64_t now_us = esp_timer_get_time();
  const uint64_t interval_us = now_us - this->last_awake_us_;
  const uint64_t sleep_total_us = s_sleep_total_us;
  const uint32_t sleep_entries = s_sleep_entries;
  const int64_t last_sleep_us = s_last_sleep_us;

  const uint64_t delta_sleep_us = sleep_total_us - this->last_sleep_total_us_;
  const uint32_t delta_entries = sleep_entries - this->last_sleep_entries_;
  const uint64_t awake_us = interval_us > delta_sleep_us ? interval_us - delta_sleep_us : 0;
  const float sleep_ratio = interval_us > 0 ? (100.0f * static_cast<float>(delta_sleep_us) / static_cast<float>(interval_us)) : 0.0f;

  ESP_LOGI(TAG,
           "[SLEEP STATS] entries=%lu | sleep=%.3f s | awake=%.3f s | sleep_ratio=%.1f%% | last_sleep=%.3f ms | total_entries=%lu | total_sleep=%.1f s",
           (unsigned long) delta_entries, delta_sleep_us / 1000000.0, awake_us / 1000000.0, sleep_ratio,
           last_sleep_us / 1000.0, (unsigned long) sleep_entries, sleep_total_us / 1000000.0);

  if (delta_entries == 0) {
    // If Zigbee changed RxOnWhenIdle again, repair it and report the state.
    enforce_zigbee_sleepy_();
    dump_zigbee_sleep_state_();
    ESP_LOGW(TAG, "[PM LOCKS] No light sleep detected; active PM locks follow:");
    esp_pm_dump_locks(stdout);
  }

  this->last_debug_ms_ = now_ms;
  this->last_awake_us_ = now_us;
  this->last_sleep_total_us_ = sleep_total_us;
  this->last_sleep_entries_ = sleep_entries;
#endif
}

void PowerManagementComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Power Management:");
  ESP_LOGCONFIG(TAG, "  Automatic light sleep: %s", YESNO(this->enable_light_sleep_));
  ESP_LOGCONFIG(TAG, "  Power down peripherals: %s", YESNO(this->power_down_peripherals_));
  ESP_LOGCONFIG(TAG, "  Start delay: %lu ms", (unsigned long) this->start_delay_ms_);
  ESP_LOGCONFIG(TAG, "  Sleep debug: %s", YESNO(this->sleep_debug_));
  if (this->sleep_debug_)
    ESP_LOGCONFIG(TAG, "  Sleep stats interval: %lu ms", (unsigned long) this->sleep_debug_interval_ms_);
}

}  // namespace power_management
}  // namespace esphome
