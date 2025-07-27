import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, uart
from esphome.const import CONF_ID, UNIT_MICROGRAMS_PER_CUBIC_METER, ICON_CHEMICAL_WEAPON

zh03b_ns = cg.esphome_ns.namespace('zh03b')
ZH03BSensor = zh03b_ns.class_('ZH03BSensor', cg.Component, uart.UARTDevice)

CONF_PM_1_0 = "pm_1_0"
CONF_PM_2_5 = "pm_2_5"
CONF_PM_10_0 = "pm_10_0"
CONF_MODE = "mode"

ZH03BMode = zh03b_ns.enum("ZH03BMode")

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ZH03BSensor),
    cv.Optional(CONF_PM_1_0): sensor.sensor_schema(
        unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER,
        icon=ICON_CHEMICAL_WEAPON,
        accuracy_decimals=0,
    ),
    cv.Optional(CONF_PM_2_5): sensor.sensor_schema(
        unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER,
        icon=ICON_CHEMICAL_WEAPON,
        accuracy_decimals=0,
    ),
    cv.Optional(CONF_PM_10_0): sensor.sensor_schema(
        unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER,
        icon=ICON_CHEMICAL_WEAPON,
        accuracy_decimals=0,
    ),
    cv.Optional(CONF_MODE, default='passive'): cv.one_of('passive', 'qa', lower=True),
}).extend(uart.UART_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    if CONF_PM_1_0 in config:
        sens = await sensor.new_sensor(config[CONF_PM_1_0])
        cg.add(var.set_pm_1_0_sensor(sens))
    if CONF_PM_2_5 in config:
        sens = await sensor.new_sensor(config[CONF_PM_2_5])
        cg.add(var.set_pm_2_5_sensor(sens))
    if CONF_PM_10_0 in config:
        sens = await sensor.new_sensor(config[CONF_PM_10_0])
        cg.add(var.set_pm_10_0_sensor(sens))
    # Set mode
    if config[CONF_MODE] == 'qa':
        cg.add(var.set_mode(ZH03BMode.MODE_QA))
    else:
        cg.add(var.set_mode(ZH03BMode.MODE_PASSIVE))
