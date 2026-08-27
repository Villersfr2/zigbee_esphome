#include "power_management.h"

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "ezbee/nwk.h"
#include <stdio.h>
#endif

namespace esphome {
namespace power_management {

static const char *const TAG = "power_management";
static const char *const NVS_NS = "pm_sleep_test";
static const uint32_t RESULT_MAGIC = 0x534C5054;  // SLPT

struct StoredSleepResult {
  uint32_t magic;
  uint32_t entries;
  uint64_t elapsed_us;
  uint64_t sleep_us;
  uint64_t awake_us;
  int64_t last_sleep_us;
};

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

#ifdef CONFIG_PM_ENABLE
static void sleep_test_timer_cb_(void *arg) {
  auto *component = static_cast<PowerManagementComponent *>(arg);
  component->finish_sleep_test_from_timer();
}
#endif

static bool load_result_(StoredSleepResult *result) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NS, NVS_READONLY, &handle) != ESP_OK)
    return false;
  size_t len = sizeof(*result);
  esp_err_t err = nvs_get_blob(handle, "result", result, &len);
  nvs_close(handle);
  return err == ESP_OK && len == sizeof(*result) && result->magic == RESULT_MAGIC;
}

static bool save_result_(const StoredSleepResult &result) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NS, NVS_READWRITE, &handle) != ESP_OK)
    return false;
  esp_err_t err = nvs_set_blob(handle, "result", &result, sizeof(result));
  if (err == ESP_OK)
    err = nvs_commit(handle);
  nvs_close(handle);
  return err == ESP_OK;
}

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
#ifdef CONFIG_PM_ENABLE
  StoredSleepResult stored{};
  if (load_result_(&stored)) {
    this->result_elapsed_us_ = stored.elapsed_us;
    this->result_sleep_us_ = stored.sleep_us;
    this->result_awake_us_ = stored.awake_us;
    this->result_last_sleep_us_ = stored.last_sleep_us;
    this->result_entries_ = stored.entries;
    this->result_ratio_ = stored.elapsed_us > 0
                              ? (100.0f * static_cast<float>(stored.sleep_us) / static_cast<float>(stored.elapsed_us))
                              : 0.0f;
    this->test_finished_ = true;
    this->enable_light_sleep_ = false;
    ESP_LOGW(TAG, "[SLEEP TEST] Saved result found in NVS. Light sleep will stay OFF so USB remains available.");
    this->print_stored_test_result_();
    this->set_interval("sleep_saved_result", 10000, [this]() { this->print_stored_test_result_(); });
    return;
  }
#endif

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

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
  s_sleep_total_us = 0;
  s_sleep_entries = 0;
  s_last_sleep_us = 0;

  esp_pm_sleep_cbs_register_config_t cbs = {};
  cbs.enter_cb = pm_sleep_enter_cb;
  cbs.exit_cb = pm_sleep_exit_cb;
  cbs.enter_cb_user_arg = this;
  cbs.exit_cb_user_arg = this;
  esp_err_t cb_err = esp_pm_light_sleep_register_cbs(&cbs);
  if (cb_err != ESP_OK) {
    ESP_LOGE(TAG, "Could not register light-sleep callbacks: %s", esp_err_to_name(cb_err));
    this->mark_failed();
    return;
  }
#endif

  // Native ESP-IDF one-shot timer. Automatic PM light sleep treats a pending esp_timer
  // as a wake deadline and wakes the chip in time to dispatch this callback.
  esp_timer_create_args_t timer_args = {};
  timer_args.callback = &sleep_test_timer_cb_;
  timer_args.arg = this;
  timer_args.dispatch_method = ESP_TIMER_TASK;
  timer_args.name = "pm_test_end";
  timer_args.skip_unhandled_events = false;  // Important: this timer MUST wake light sleep.

  esp_err_t timer_err = esp_timer_create(&timer_args, &this->test_timer_);
  if (timer_err != ESP_OK) {
    ESP_LOGE(TAG, "Could not create 120 s wake timer: %s", esp_err_to_name(timer_err));
    this->mark_failed();
    return;
  }

  timer_err = esp_timer_start_once(this->test_timer_, static_cast<uint64_t>(this->test_duration_ms_) * 1000ULL);
  if (timer_err != ESP_OK) {
    ESP_LOGE(TAG, "Could not start 120 s wake timer: %s", esp_err_to_name(timer_err));
    esp_timer_delete(this->test_timer_);
    this->test_timer_ = nullptr;
    this->mark_failed();
    return;
  }

  this->test_start_us_ = esp_timer_get_time();
  this->pm_start_ms_ = millis();
  this->test_finished_ = false;

  const int cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  esp_pm_config_t pm_config = {
      .max_freq_mhz = cpu_freq_mhz,
      .min_freq_mhz = cpu_freq_mhz,
      .light_sleep_enable = true,
  };
  esp_err_t err = esp_pm_configure(&pm_config);
  if (err == ESP_OK) {
    this->pm_configured_ = true;
    ESP_LOGI(TAG, "Automatic light sleep enabled at %d MHz", cpu_freq_mhz);
    ESP_LOGI(TAG, "[SLEEP TEST] Native ESP-IDF wake timer armed for 120 seconds (wake-capable, skip_unhandled_events=FALSE)");
    ESP_LOGI(TAG, "[SLEEP TEST] Timer callback will save result to NVS and call esp_restart(); ESPHome loop() is NOT used to end the test");
    ESP_LOGI(TAG, "[SLEEP TEST] After reboot, result remains in NVS and repeats every 10 seconds indefinitely");
    dump_zigbee_sleep_state_();
  } else {
    ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
    esp_timer_stop(this->test_timer_);
    esp_timer_delete(this->test_timer_);
    this->test_timer_ = nullptr;
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

void PowerManagementComponent::finish_sleep_test_from_timer() {
#ifdef CONFIG_PM_ENABLE
  // Runs from the ESP timer task after the native timer wakes automatic light sleep.
  // NVS and esp_restart() are intentionally done in task context, not ISR context.
  this->stop_test_and_report_();
#endif
}

void PowerManagementComponent::print_stored_test_result_() {
  ESP_LOGW(TAG, "============================================================");
  ESP_LOGW(TAG, "[SLEEP TEST SAVED RESULT]");
  ESP_LOGW(TAG, "duration=%.3f s | entries=%lu | sleep=%.3f s | awake=%.3f s | sleep_ratio=%.1f%% | last_sleep=%.3f ms",
           this->result_elapsed_us_ / 1000000.0, (unsigned long) this->result_entries_,
           this->result_sleep_us_ / 1000000.0, this->result_awake_us_ / 1000000.0,
           this->result_ratio_, this->result_last_sleep_us_ / 1000.0);
  ESP_LOGW(TAG, "Result remains stored in NVS and repeats every 10 seconds. Light sleep is OFF on this boot.");
  ESP_LOGW(TAG, "============================================================");
}

void PowerManagementComponent::stop_test_and_report_() {
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_LIGHT_SLEEP_CALLBACKS
  if (!this->pm_configured_ || this->test_finished_)
    return;

  const uint64_t end_us = esp_timer_get_time();
  StoredSleepResult result{};
  result.magic = RESULT_MAGIC;
  result.entries = s_sleep_entries;
  result.elapsed_us = end_us - this->test_start_us_;
  result.sleep_us = s_sleep_total_us;
  result.awake_us = result.elapsed_us > result.sleep_us ? result.elapsed_us - result.sleep_us : 0;
  result.last_sleep_us = s_last_sleep_us;

  this->test_finished_ = true;
  this->pm_configured_ = false;

  if (this->test_timer_ != nullptr) {
    esp_timer_delete(this->test_timer_);
    this->test_timer_ = nullptr;
  }

  if (save_result_(result)) {
    esp_restart();
  } else {
    const int cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    esp_pm_config_t awake_config = {
        .max_freq_mhz = cpu_freq_mhz,
        .min_freq_mhz = cpu_freq_mhz,
        .light_sleep_enable = false,
    };
    esp_pm_configure(&awake_config);
    ESP_LOGE(TAG, "[SLEEP TEST] Could not save result to NVS; automatic restart cancelled");
  }
#endif
}

void PowerManagementComponent::loop() {
  // Intentionally empty for the diagnostic test. The 120 s completion path is driven
  // entirely by the native wake-capable esp_timer, so ESPHome loop scheduling cannot
  // prevent the test from ending.
}

void PowerManagementComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Power Management:");
  ESP_LOGCONFIG(TAG, "  Automatic light sleep: %s", YESNO(this->enable_light_sleep_));
  ESP_LOGCONFIG(TAG, "  Power down peripherals: %s", YESNO(this->power_down_peripherals_));
  ESP_LOGCONFIG(TAG, "  Start delay: %lu ms", (unsigned long) this->start_delay_ms_);
  ESP_LOGCONFIG(TAG, "  Sleep debug: %s", YESNO(this->sleep_debug_));
  if (this->sleep_debug_)
    ESP_LOGCONFIG(TAG, "  Test mode: native 120 s ESP timer -> wake -> NVS -> reboot -> result every 10 s");
}

}  // namespace power_management
}  // namespace esphome
