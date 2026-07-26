#include "zigbee_time.h"
#include "esphome/core/log.h"

namespace esphome {
namespace zigbee {

// This time standard is the number of
// seconds since 0 hrs 0 mins 0 sec on 1st January 2000 UTC (Universal Coordinated Time).
constexpr time_t EPOCH_2000 = 946684800;

ZigbeeTime *global_time = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void ZigbeeTime::setup() {
  global_time = this;
  ezb_zcl_time_interface_t time_interface = {
      .get_utc_time = this->get_utc_time,
      .set_utc_time = this->set_utc_time,
  };
  ezb_zcl_time_server_interface_register(this->time_ep_, time_interface);
  this->parent_->add_on_join_callback([this](bool x) { this->update(); });
  ESP_LOGD(TAG, "Using Zigbee network as time source");
}

void ZigbeeTime::cb(ezb_err_t status) {
  if (status == EZB_ERR_NONE) {
    ESP_LOGI(TAG, "Time synchronization successful");
  } else if (status == EZB_ERR_TIMEOUT) {
    ESP_LOGW(TAG, "Time synchronization timed out");
  } else {
    ESP_LOGW(TAG, "Time synchronization failed with error: %d", status);
  }
}

void ZigbeeTime::update() {
  if (this->parent_->is_connected()) {
    if (esp_zigbee_lock_acquire(20 / portTICK_PERIOD_MS)) {
      ESP_LOGD(TAG, "Updating time sync from Zigbee network...");
      ezb_zcl_time_server_synchronize_time(this->time_ep_, 10, this->cb, EZB_ZCL_TIME_SERVER_RANK_MASTER);
      esp_zigbee_lock_release();
    } else {
      ESP_LOGW(TAG, "Could not acquire Zigbee lock to synchronize time, will retry...");
      this->defer([this]() { this->update(); });
    }
  } else {
    ESP_LOGD(TAG, "Not connected to Zigbee network, cannot synchronize time");
  }
}

uint32_t ZigbeeTime::get_utc_time() { return (uint32_t) (global_time->timestamp_now() - EPOCH_2000); }

void ZigbeeTime::set_utc_time(uint32_t utc) {
  ESP_LOGI(TAG, "Received time synchronization request with UTC time: %u", utc + EPOCH_2000);
  global_time->set_epoch_time(utc + EPOCH_2000);
}

void ZigbeeTime::set_epoch_time(uint32_t utc) {
  // called from zigbee task, defer to main loop
  this->defer([this, utc]() {
    ESP_LOGI(TAG, "Setting device time to UTC: %u", utc);
    this->synchronize_epoch_(utc);
    this->has_time_ = true;
  });
}

}  // namespace zigbee
}  // namespace esphome