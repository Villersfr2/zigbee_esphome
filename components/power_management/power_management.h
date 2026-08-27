#pragma once

#include <cstdint>

#include "esphome/core/component.h"

namespace esphome {
namespace power_management {

class PowerManagementComponent : public Component {
 public:
  void setup() override;
  void dump_config() override;

  void set_enable_light_sleep(bool enable) { this->enable_light_sleep_ = enable; }
  void set_power_down_peripherals(bool enable) { this->power_down_peripherals_ = enable; }
  void set_start_delay(uint32_t delay_ms) { this->start_delay_ms_ = delay_ms; }

 protected:
  void configure_pm_();

  bool enable_light_sleep_{true};
  bool power_down_peripherals_{true};
  uint32_t start_delay_ms_{30000};
};

}  // namespace power_management
}  // namespace esphome
