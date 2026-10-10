import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_PH,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_HOUR,
    UNIT_MILLIVOLT,
)

from .. import CONF_EMEC_ID, EMEC_PLATFORM_SCHEMA, emec_field, emec_ns

DEPENDENCIES = ["emec"]

EmecSensor = emec_ns.class_("EmecSensor", sensor.Sensor)

# key -> (protocol field, scale, sensor schema)
SENSORS = {
    "ph": (
        "F_PH",
        0.01,
        sensor.sensor_schema(
            EmecSensor,
            icon="mdi:ph",
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_PH,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    ),
    "redox": (
        "F_MV",
        1.0,
        sensor.sensor_schema(
            EmecSensor,
            unit_of_measurement=UNIT_MILLIVOLT,
            icon="mdi:flash-triangle",
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    ),
    "service_hours": (
        "F_SERVICE_HOURS",
        1.0,
        sensor.sensor_schema(
            EmecSensor,
            unit_of_measurement=UNIT_HOUR,
            icon="mdi:timer-outline",
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
    "service_offset": (
        "F_SERVICE_OFFSET",
        1.0,
        sensor.sensor_schema(
            EmecSensor,
            icon="mdi:tune",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
    # Raw, not yet understood fields of the `valuer` reply.
    "valuer_field3": (
        "F_DOSE",
        1.0,
        sensor.sensor_schema(
            EmecSensor,
            icon="mdi:code-braces",
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
    "valuer_field4": (
        "F_VALUE_FLAG",
        1.0,
        sensor.sensor_schema(
            EmecSensor,
            icon="mdi:code-braces",
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
}

CONFIG_SCHEMA = EMEC_PLATFORM_SCHEMA.extend(
    {cv.Optional(key): schema for key, (_, _, schema) in SENSORS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_EMEC_ID])
    for key, (field, scale, _) in SENSORS.items():
        if key not in config:
            continue
        var = await sensor.new_sensor(config[key])
        cg.add(var.set_scale(scale))
        cg.add(hub.register_listener(emec_field(field), var, True))
