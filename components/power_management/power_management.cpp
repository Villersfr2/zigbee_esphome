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
    this->test_finished_ = false;
    this->pm_start_ms_ = millis();
    this->last_debug_ms_ = this->pm_start_ms_;
    this->last_awake_us_ = esp_timer_get_time();
    this->test_start_us_ = this->last_awake_us_;
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    s_sleep_total_us = 0;
    s_sleep_entries = 0;
    s_last_sleep_us = 0;
#endif
    this->last_sleep_total_us_ = 0;
    this->last_sleep_entries_ = 0;

    ESP_LOGI(TAG, "Automatic light sleep enabled at %d MHz (start delay: %lu ms)", cpu_freq_mhz,
             (unsigned long) this->start_delay_ms_);
    ESP_LOGI(TAG, "[SLEEP TEST] Light sleep will run for 120 seconds, then be disabled automatically");
    ESP_LOGI(TAG, "[SLEEP TEST] After wake, stored result will repeat every 10 seconds for 2 minutes");
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
        ESP_LOGI(TAG, "Real light-sleep accounting enabled (ESP-IDF callbacks)");
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

void PowerManagementComponent::print_stored_test_result_() {
  ESP_LOGW(TAG, "============================================================");
  ESP_LOGW(TAG, "[SLEEP TEST COMPLETE] automatic light sleep is OFF");
  ESP_LOGW(TAG, "[SLEEP TEST RESULT] duration=%.3f s | entries=%lu | sleep=%.3f s | awake=%.3f s | sleep_ratio=%.1f%% | last_sleep=%.3f ms",
           this->result_elapsed_us_ / 1000000.0, (unsigned long) this->result_entries_,
           this->result_sleep_us_ / 1000000.0, this->result_awake_us_ / 1000000.0,
           this->result_ratio_, this->result_last_sleep_us_ / 1000.0);
  ESP_LOGW(TAG, "[SLEEP TEST] This stored result repeats every 10 s so you can reconnect USB serial manually");
  ESP_LOGW(TAG, "============================================================");
}

void PowerManagementComponent::stop_test_and_report_() {
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_LIGHT_SLEEP_CALLBACKS
  if (!this->pm_configured_ || this->test_finished_)
    return;

  const int cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  esp_pm_config_t awake_config = {
      .max_freq_mhz = cpu_freq_mhz,
      .min_freq_mhz = cpu_freq_mhz,
      .light_sleep_enable = false,
  };
  esp_err_t err = esp_pm_configure(&awake_config);
  this->test_finished_ = true;
  this->pm_configured_ = false;

  const uint64_t end_us = esp_timer_get_time();
  this->result_elapsed_us_ = end_us - this->test_start_us_;
  this->result_sleep_us_ = s_sleep_total_us;
  this->result_awake_us_ = this->result_elapsed_us_ > this->result_sleep_us_ ? this->result_elapsed_us_ - this->result_sleep_us_ : 0;
  this->result_entries_ = s_sleep_entries;
  this->result_last_sleep_us_ = s_last_sleep_us;
  this->result_ratio_ = this->result_elapsed_us_ > 0
                            ? (100.0f * static_cast<float>(this->result_sleep_us_) / static_cast<float>(this->result_elapsed_us_))
                            : 0.0f;

  // First report after 2 s, then every 10 s for two minutes (12 additional reports).
  this->set_timeout("sleep_test_first_report", 2000, [this, err]() {
    ESP_LOGW(TAG, "[SLEEP TEST] esp_pm_configure(light_sleep=false): %s", esp_err_to_name(err));
    this->print_stored_test_result_();
    dump_zigbee_sleep_state_();
    ESP_LOGW(TAG, "[PM LOCKS] Final power-management lock dump follows:");
    esp_pm_dump_locks(stdout);
  });

  for (uint32_t i = 1; i <= 12; i++) {
    const uint32_t delay_ms = 10000 * i;
    this->set_timeout(delay_ms, [this]() { this->print_stored_test_result_(); });
  }
#endif
}

void PowerManagementComponent::loop() {
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_LIGHT_SLEEP_CALLBACKS
  if (!this->pm_configured_ || this->test_finished_)
    return;

  const uint32_t now_ms = millis();
  if ((uint32_t) (now_ms - this->pm_start_ms_) >= this->test_duration_ms_) {
    this->stop_test_and_report_();
    return;
  }

  // No periodic logs during the 120 s measurement. The callbacks accumulate
  // sleep statistics in RAM; reporting begins only after light sleep is disabled.
#endif
}

void PowerManagementComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Power Management:");
  ESP_LOGCONFIG(TAG, "  Automatic light sleep: %s", YESNO(this->enable_light_sleep_));
  ESP_LOGCONFIG(TAG, "  Power down peripherals: %s", YESNO(this->power_down_peripherals_));
  ESP_LOGCONFIG(TAG, "  Start delay: %lu ms", (unsigned long) this->start_delay_ms_);
  ESP_LOGCONFIG(TAG, "  Sleep debug: %s", YESNO(this->sleep_debug_));
  if (this->sleep_debug_)
    ESP_LOGCONFIG(TAG, "  Test mode: 120 s sleep, then result every 10 s for 2 min");
}

}  // namespace power_management
}  // namespace esphome
