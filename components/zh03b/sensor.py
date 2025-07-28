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

def validate_zh03b_config(config):
    """Validate that update_interval is only used with Q&A mode"""
    if config.get(CONF_MODE) == "PASSIVE" and CONF_UPDATE_INTERVAL in config:
        raise cv.Invalid("update_interval can only be used with Q&A mode. "
                        "In PASSIVE mode, the sensor sends data automatically every second.")
    return config

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ZH03BSensor),
            cv.Optional(CONF_MODE, default="PASSIVE"): cv.enum(MODES, upper=True),
            cv.Optional(CONF_UPDATE_INTERVAL, default="60s"): cv.positive_time_period_milliseconds,
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
    .extend(uart.UART_DEVICE_SCHEMA),
    validate_zh03b_config,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # Set mode first
    cg.add(var.set_mode(config[CONF_MODE]))

    # Set update interval only for Q&A mode
    if config[CONF_MODE] == "QA" and CONF_UPDATE_INTERVAL in config:
        interval = config[CONF_UPDATE_INTERVAL]
        # Convert to milliseconds and ensure minimum 30s
        interval_ms = max(30000, int(interval.total_milliseconds))
        cg.add(var.set_update_interval(interval_ms))
    elif config[CONF_MODE] == "PASSIVE" and CONF_UPDATE_INTERVAL in config:
        # Log warning if update_interval is set in passive mode
        cg.add_build_flag("-DESPHOME_LOG_LEVEL_WARN")
        
    if CONF_PM_1_0 in config:
        sens = await sensor.new_sensor(config[CONF_PM_1_0])
        cg.add(var.set_pm_1_0_sensor(sens))

    if CONF_PM_2_5 in config:
        sens = await sensor.new_sensor(config[CONF_PM_2_5])
        cg.add(var.set_pm_2_5_sensor(sens))

    if CONF_PM_10_0 in config:
        sens = await sensor.new_sensor(config[CONF_PM_10_0])
        cg.add(var.set_pm_10_0_sensor(sens))
