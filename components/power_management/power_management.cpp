#include "power_management.h"

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#endif

namespace esphome {
namespace power_management {

static const char *const TAG = "power_management";

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
      ESP_LOGW(TAG, "Sleep debug enabled: periodic timing statistics every %lu ms. Logging exact automatic light-sleep entry/exit would itself disturb sleep.",
               (unsigned long) this->sleep_debug_interval_ms_);
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
#ifdef CONFIG_PM_ENABLE
  if (!this->pm_configured_ || !this->sleep_debug_)
    return;

  const uint32_t now_ms = millis();
  if ((uint32_t) (now_ms - this->last_debug_ms_) < this->sleep_debug_interval_ms_)
    return;

  const uint64_t now_us = esp_timer_get_time();
  const uint64_t elapsed_us = now_us - this->last_awake_us_;
  ESP_LOGD(TAG, "[SLEEP DEBUG] PM active; elapsed wall time: %llu us; automatic light sleep may occur during idle ticks",
           (unsigned long long) elapsed_us);
  this->last_debug_ms_ = now_ms;
  this->last_awake_us_ = now_us;
#endif
}

void PowerManagementComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Power Management:");
  ESP_LOGCONFIG(TAG, "  Automatic light sleep: %s", YESNO(this->enable_light_sleep_));
  ESP_LOGCONFIG(TAG, "  Power down peripherals: %s", YESNO(this->power_down_peripherals_));
  ESP_LOGCONFIG(TAG, "  Start delay: %lu ms", (unsigned long) this->start_delay_ms_);
  ESP_LOGCONFIG(TAG, "  Sleep debug: %s", YESNO(this->sleep_debug_));
  if (this->sleep_debug_)
    ESP_LOGCONFIG(TAG, "  Sleep debug interval: %lu ms", (unsigned long) this->sleep_debug_interval_ms_);
}

}  // namespace power_management
}  // namespace esphome
