import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, uart
from esphome.const import (
    CONF_ID,
    CONF_PM_2_5,
    CONF_PM_10_0,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_PM25,
    DEVICE_CLASS_PM10,
    STATE_CLASS_MEASUREMENT,
    UNIT_MICROGRAMS_PER_CUBIC_METER,
)

CONF_PM_1_0 = "pm_1_0"
CONF_MODE = "mode"
DEVICE_CLASS_PM1 = "pm1"

DEPENDENCIES = ["uart"]

zh03b_ns = cg.esphome_ns.namespace("zh03b")
ZH03BSensor = zh03b_ns.class_("ZH03BSensor", cg.Component, uart.UARTDevice)
ZH03BMode = zh03b_ns.enum("ZH03BMode")

MODES = {
    "PASSIVE": ZH03BMode.MODE_PASSIVE,
    "QA": ZH03BMode.MODE_QA,
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ZH03BSensor),
            cv.Optional(CONF_MODE, default="PASSIVE"): cv.enum(MODES, upper=True),
            cv.Optional(CONF_UPDATE_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_PM_1_0): sensor.sensor_schema(
                unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER,
                icon="mdi:chemical-weapon",
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_PM1,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_PM_2_5): sensor.sensor_schema(
                unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER,
                icon="mdi:chemical-weapon",
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_PM25,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_PM_10_0): sensor.sensor_schema(
                unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER,
                icon="mdi:chemical-weapon",
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_PM10,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # Set mode dan update interval
    cg.add(var.set_mode(config[CONF_MODE]))
    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))

    if CONF_PM_1_0 in config:
        sens = await sensor.new_sensor(config[CONF_PM_1_0])
        cg.add(var.set_pm_1_0_sensor(sens))

    if CONF_PM_2_5 in config:
        sens = await sensor.new_sensor(config[CONF_PM_2_5])
        cg.add(var.set_pm_2_5_sensor(sens))

    if CONF_PM_10_0 in config:
        sens = await sensor.new_sensor(config[CONF_PM_10_0])
        cg.add(var.set_pm_10_0_sensor(sens))