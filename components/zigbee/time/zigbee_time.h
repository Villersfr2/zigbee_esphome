#pragma once

#include "esphome/core/component.h"
#include "esphome/components/time/real_time_clock.h"
#include "../zigbee.h"

namespace esphome {
namespace zigbee {

class ZigBeeComponent;

class ZigbeeTime : public time::RealTimeClock {
 public:
  ZigbeeTime(ZigBeeComponent *parent) : parent_(parent) {}
  void setup() override;
  void update() override;
  void set_epoch_time(uint32_t utc);

 protected:
  static void set_utc_time(uint32_t utc);
  static uint32_t get_utc_time();
  static void cb(ezb_err_t status);
  bool has_time_{false};
  uint8_t time_ep_{1};
  ZigBeeComponent *parent_;
};

}  // namespace zigbee
}  // namespace esphome
