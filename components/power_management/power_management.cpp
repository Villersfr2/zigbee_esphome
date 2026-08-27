#include "power_management.h"

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "ezbee/nwk.h"
#endif

namespace esphome {
namespace power_management {

static const char *const TAG = "power_management";

static void dump_zigbee_sleep_state_() {
  const bool rx_on = ezb_nwk_get_rx_on_when_idle();
  const ezb_shortaddr_t short_addr = ezb_nwk_get_short_address();
  const bool joined = short_addr != EZB_NWK_ADDR_UNKNOWN;
  ESP_LOGI(TAG, "[ZIGBEE SLEEP] joined=%s | short_addr=0x%04x | sleepy=%s | rx_on_when_idle=%s | keepalive=%lu ms",
           YESNO(joined), short_addr, YESNO(!rx_on), rx_on ? "TRUE" : "FALSE",
           (unsigned long) ezb_nwk_get_keepalive_interval());
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
  // IMPORTANT: rx_on_when_idle must be configured by the Zigbee component before
  // joining the network. Espressif documents that it must not be changed after join.
  // This component therefore only verifies the live state; it no longer forces it.
  dump_zigbee_sleep_state_();

  const int cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  esp_pm_config_t pm_config = {
      .max_freq_mhz = cpu_freq_mhz,
      .min_freq_mhz = cpu_freq_mhz,
      .light_sleep_enable = true,
  };

  const esp_err_t err = esp_pm_configure(&pm_config);
  if (err == ESP_OK) {
    this->pm_configured_ = true;
    ESP_LOGI(TAG, "Automatic light sleep enabled at %d MHz", cpu_freq_mhz);
    ESP_LOGI(TAG, "Native Zigbee/ESP-IDF sleep management active; no diagnostic timer, NVS or forced reboot");
    ESP_LOGI(TAG, "USB Serial/JTAG may become unavailable during light sleep; this does not mean Zigbee stopped");
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
  // Automatic light sleep is handled by ESP-IDF tickless idle and the Zigbee stack.
}

void PowerManagementComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Power Management:");
  ESP_LOGCONFIG(TAG, "  Automatic light sleep: %s", YESNO(this->enable_light_sleep_));
  ESP_LOGCONFIG(TAG, "  Power down peripherals: %s", YESNO(this->power_down_peripherals_));
  ESP_LOGCONFIG(TAG, "  Start delay: %lu ms", (unsigned long) this->start_delay_ms_);
  ESP_LOGCONFIG(TAG, "  Native Zigbee/ESP-IDF sleepy mode: YES");
  ESP_LOGCONFIG(TAG, "  Diagnostic timer/reboot/NVS: REMOVED");
}

}  // namespace power_management
}  // namespace esphome
