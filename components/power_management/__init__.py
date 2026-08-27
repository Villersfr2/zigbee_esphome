from __future__ import annotations

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]

CONF_ENABLE_LIGHT_SLEEP = "enable_light_sleep"
CONF_POWER_DOWN_PERIPHERALS = "power_down_peripherals"
CONF_START_DELAY = "start_delay"

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
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # Required by ESP-IDF automatic light sleep. Merely compiling PM support is not
    # sufficient; the C++ component calls esp_pm_configure() at runtime.
    add_idf_sdkconfig_option("CONFIG_PM_ENABLE", True)
    add_idf_sdkconfig_option("CONFIG_FREERTOS_USE_TICKLESS_IDLE", True)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_enable_light_sleep(config[CONF_ENABLE_LIGHT_SLEEP]))
    cg.add(var.set_power_down_peripherals(config[CONF_POWER_DOWN_PERIPHERALS]))
    cg.add(var.set_start_delay(config[CONF_START_DELAY]))
