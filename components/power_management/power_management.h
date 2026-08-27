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

  bool enable_light_sleep_{true};
  bool power_down_peripherals_{true};
  bool sleep_debug_{false};
  bool pm_configured_{false};
  uint32_t start_delay_ms_{30000};
  uint32_t sleep_debug_interval_ms_{10000};
};

}  // namespace power_management
}  // namespace esphome
