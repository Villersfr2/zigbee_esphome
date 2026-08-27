#pragma once

#include <cstdint>

#include "esphome/core/component.h"

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

 protected:
  void configure_pm_();
  void stop_test_and_report_();

  bool enable_light_sleep_{true};
  bool power_down_peripherals_{true};
  bool sleep_debug_{false};
  bool pm_configured_{false};
  bool test_finished_{false};
  uint32_t start_delay_ms_{30000};
  uint32_t sleep_debug_interval_ms_{10000};
  uint32_t test_duration_ms_{120000};
  uint32_t pm_start_ms_{0};
  uint32_t last_debug_ms_{0};
  uint64_t last_awake_us_{0};
  uint64_t test_start_us_{0};
  uint64_t last_sleep_total_us_{0};
  uint32_t last_sleep_entries_{0};
};

}  // namespace power_management
}  // namespace esphome
