import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from .. import CONF_EMEC_ID, EMEC_PLATFORM_SCHEMA, emec_field, emec_ns

DEPENDENCIES = ["emec"]

EmecBinarySensor = emec_ns.class_("EmecBinarySensor", binary_sensor.BinarySensor)

CONF_CONNECTED = "connected"
CONF_PH_OUTLIER_ALARM = "ph_outlier_alarm"
CONF_REDOX_OUTLIER_ALARM = "redox_outlier_alarm"


def _alarm(icon, diagnostic=False):
    kwargs = {"icon": icon, "device_class": DEVICE_CLASS_PROBLEM}
    if diagnostic:
        kwargs["entity_category"] = ENTITY_CATEGORY_DIAGNOSTIC
    return binary_sensor.binary_sensor_schema(EmecBinarySensor, **kwargs)


def _diag(icon):
    return binary_sensor.binary_sensor_schema(
        EmecBinarySensor, icon=icon, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    )


# key -> (protocol field, schema). A binary sensor is ON when the field is != 0.
BINARY_SENSORS = {
    # alarm flags (allrmr): a1, a2, a3 and a8 are confirmed
    "alarm_ph_empty": ("F_ALARM_1", _alarm("mdi:water-off")),
    "alarm_chlorine_empty": ("F_ALARM_2", _alarm("mdi:water-off")),
    "alarm_no_flow": ("F_ALARM_3", _alarm("mdi:water-off")),
    "alarm_4": ("F_ALARM_4", _alarm("mdi:alert-outline", True)),
    "alarm_5": ("F_ALARM_5", _alarm("mdi:alert-outline", True)),
    "alarm_6": ("F_ALARM_6", _alarm("mdi:alert-outline", True)),
    "alarm_7": ("F_ALARM_7", _alarm("mdi:alert-outline", True)),
    "standby": (
        "F_ALARM_8",
        binary_sensor.binary_sensor_schema(EmecBinarySensor, icon="mdi:power-standby"),
    ),
    # outputs (outptr)
    "output_1": ("F_OUT1", _diag("mdi:electric-switch")),
    "output_2": ("F_OUT2", _diag("mdi:electric-switch")),
    "output_3": ("F_OUT3", _diag("mdi:electric-switch")),
    # feature enabled = its threshold is > 0 minutes
    "dosing_alarm_ph_enabled": ("F_DOSAL_PH_MIN", _diag("mdi:alarm")),
    "dosing_alarm_redox_enabled": ("F_DOSAL_MV_MIN", _diag("mdi:alarm")),
    "probe_failure_ph_enabled": ("F_PRBFAIL_PH_MIN", _diag("mdi:alarm-check")),
    "probe_failure_redox_enabled": ("F_PRBFAIL_MV_MIN", _diag("mdi:alarm-check")),
}

CONFIG_SCHEMA = EMEC_PLATFORM_SCHEMA.extend(
    {cv.Optional(key): schema for key, (_, schema) in BINARY_SENSORS.items()}
).extend(
    {
        # ON while the controller answers the read cycle.
        cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(
            EmecBinarySensor,
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # ON when the reading stayed off by more than the filter threshold for
        # more than `max_outliers` readings (see `ph_filter` / `redox_filter`).
        cv.Optional(CONF_PH_OUTLIER_ALARM): _alarm("mdi:ph"),
        cv.Optional(CONF_REDOX_OUTLIER_ALARM): _alarm("mdi:flash-alert"),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_EMEC_ID])
    for key, (field, _) in BINARY_SENSORS.items():
        if key not in config:
            continue
        var = await binary_sensor.new_binary_sensor(config[key])
        cg.add(hub.register_listener(emec_field(field), var))
    if CONF_CONNECTED in config:
        var = await binary_sensor.new_binary_sensor(config[CONF_CONNECTED])
        cg.add(hub.register_connected_listener(var))
    if CONF_PH_OUTLIER_ALARM in config:
        var = await binary_sensor.new_binary_sensor(config[CONF_PH_OUTLIER_ALARM])
        cg.add(hub.register_ph_alarm_listener(var))
    if CONF_REDOX_OUTLIER_ALARM in config:
        var = await binary_sensor.new_binary_sensor(config[CONF_REDOX_OUTLIER_ALARM])
        cg.add(hub.register_redox_alarm_listener(var))
