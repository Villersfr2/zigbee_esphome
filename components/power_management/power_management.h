#pragma once

#include <cstdint>

#include "esphome/core/component.h"

#ifdef CONFIG_PM_ENABLE
#include "esp_timer.h"
#endif

namespace esphome {
namespace power_management {

class PowerManagementComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_enable_light_sleep(bool enable) { this->enable_light_sleep_ = enable; }
  void set_power_down_peripherals(bool enable) { this->power_down_peripherals_ = enable; }
  void set_start_delay(uint32_t delay_ms) { this->start_delay_ms_ = delay_ms; }
  void set_sleep_debug(bool enable) { this->sleep_debug_ = enable; }
  void set_sleep_debug_interval(uint32_t interval_ms) { this->sleep_debug_interval_ms_ = interval_ms; }

  // Called by the native ESP-IDF one-shot timer after it wakes automatic light sleep.
  void finish_sleep_test_from_timer();

 protected:
  void configure_pm_();
  void stop_test_and_report_();
  void print_stored_test_result_();

  bool enable_light_sleep_{true};
  bool power_down_peripherals_{true};
  bool sleep_debug_{false};
  bool pm_configured_{false};
  bool test_finished_{false};
  uint32_t start_delay_ms_{30000};
  uint32_t sleep_debug_interval_ms_{10000};
  uint32_t test_duration_ms_{120000};
  uint32_t pm_start_ms_{0};
  uint64_t test_start_us_{0};

#ifdef CONFIG_PM_ENABLE
  esp_timer_handle_t test_timer_{nullptr};
#endif

  uint64_t result_elapsed_us_{0};
  uint64_t result_sleep_us_{0};
  uint64_t result_awake_us_{0};
  int64_t result_last_sleep_us_{0};
  uint32_t result_entries_{0};
  float result_ratio_{0.0f};
};

}  // namespace power_management
}  // namespace esphome
