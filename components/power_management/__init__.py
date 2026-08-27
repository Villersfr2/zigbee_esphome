from __future__ import annotations

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]

CONF_ENABLE_LIGHT_SLEEP = "enable_light_sleep"
CONF_POWER_DOWN_PERIPHERALS = "power_down_peripherals"
CONF_START_DELAY = "start_delay"
CONF_SLEEP_DEBUG = "sleep_debug"
CONF_SLEEP_DEBUG_INTERVAL = "sleep_debug_interval"

power_management_ns = cg.esphome_ns.namespace("power_management")
PowerManagementComponent = power_management_ns.class_(
    "PowerManagementComponent", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PowerManagementComponent),
        cv.Optional(CONF_ENABLE_LIGHT_SLEEP, default=True): cv.boolean,
        cv.Optional(CONF_POWER_DOWN_PERIPHERALS, default=True): cv.boolean,
        cv.Optional(CONF_START_DELAY, default=30000): cv.int_range(min=0, max=300000),
        cv.Optional(CONF_SLEEP_DEBUG, default=False): cv.boolean,
        cv.Optional(CONF_SLEEP_DEBUG_INTERVAL, default=10000): cv.int_range(min=1000, max=300000),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    add_idf_sdkconfig_option("CONFIG_PM_ENABLE", True)
    add_idf_sdkconfig_option("CONFIG_FREERTOS_USE_TICKLESS_IDLE", True)
    # ESP-IDF exposes entry/exit callbacks for automatic light sleep when this
    # option is enabled. We use the exit callback to accumulate the actual
    # sleep duration without logging from the IDLE task.
    if config[CONF_SLEEP_DEBUG]:
        add_idf_sdkconfig_option("CONFIG_PM_LIGHT_SLEEP_CALLBACKS", True)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_enable_light_sleep(config[CONF_ENABLE_LIGHT_SLEEP]))
    cg.add(var.set_power_down_peripherals(config[CONF_POWER_DOWN_PERIPHERALS]))
    cg.add(var.set_start_delay(config[CONF_START_DELAY]))
    cg.add(var.set_sleep_debug(config[CONF_SLEEP_DEBUG]))
    cg.add(var.set_sleep_debug_interval(config[CONF_SLEEP_DEBUG_INTERVAL]))
