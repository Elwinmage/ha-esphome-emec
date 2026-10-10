import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    DEVICE_CLASS_PH,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_CONFIG,
    UNIT_MILLIVOLT,
    UNIT_MINUTE,
    UNIT_PERCENT,
)

from .. import CONF_EMEC_ID, EMEC_PLATFORM_SCHEMA, emec_field, emec_ns

DEPENDENCIES = ["emec"]

EmecNumber = emec_ns.class_("EmecNumber", number.Number)

UNIT_STROKES = "P/m"


def _num(field, min_value, max_value, step, icon, unit=None, device_class=None, scale=1.0):
    kwargs = {"icon": icon, "entity_category": ENTITY_CATEGORY_CONFIG}
    if unit is not None:
        kwargs["unit_of_measurement"] = unit
    if device_class is not None:
        kwargs["device_class"] = device_class
    return (
        field,
        min_value,
        max_value,
        step,
        scale,
        number.number_schema(EmecNumber, **kwargs),
    )


# key -> (protocol field, min, max, step, scale, schema)
# scale: entity value = on-wire integer * scale (pH setpoints are sent x100).
NUMBERS = {
    # setpnw
    "ph_high_setpoint": _num("F_PH_HIGH_SP", 0.0, 14.0, 0.05, "mdi:target", device_class=DEVICE_CLASS_PH, scale=0.01),
    "ph_low_setpoint": _num("F_PH_LOW_SP", 0.0, 14.0, 0.05, "mdi:target", device_class=DEVICE_CLASS_PH, scale=0.01),
    "ph_high_dose": _num("F_PH_HIGH_PCT", 0, 100, 1, "mdi:percent", UNIT_PERCENT),
    "ph_low_dose": _num("F_PH_LOW_PCT", 0, 100, 1, "mdi:percent", UNIT_PERCENT),
    "ph_waiting_time": _num("F_PH_WAIT", 0, 99, 1, "mdi:timer-sand", UNIT_MINUTE),
    "redox_low_setpoint": _num("F_MV_LOW_SP", 0, 999, 5, "mdi:target", UNIT_MILLIVOLT, DEVICE_CLASS_VOLTAGE),
    "redox_high_setpoint": _num("F_MV_HIGH_SP", 0, 999, 5, "mdi:target", UNIT_MILLIVOLT, DEVICE_CLASS_VOLTAGE),
    "redox_low_dose": _num("F_MV_LOW_PCT", 0, 100, 1, "mdi:percent", UNIT_PERCENT),
    "redox_high_dose": _num("F_MV_HIGH_PCT", 0, 100, 1, "mdi:percent", UNIT_PERCENT),
    "redox_waiting_time": _num("F_MV_WAIT", 0, 99, 1, "mdi:timer-sand", UNIT_MINUTE),
    # setskw
    "max_strokes_ph": _num("F_MAX_STROKES_PH", 1, 180, 1, "mdi:pump", UNIT_STROKES),
    "max_strokes_redox": _num("F_MAX_STROKES_MV", 1, 180, 1, "mdi:pump", UNIT_STROKES),
    # paramw
    "passcode": _num("F_PASSCODE", 0, 9999, 1, "mdi:key"),
    "feeding_delay": _num("F_FEEDING_DELAY", 0, 60, 1, "mdi:timer-outline", UNIT_MINUTE),
    # alldow: 0 = disabled
    "dosing_alarm_ph_threshold": _num("F_DOSAL_PH_MIN", 0, 999, 1, "mdi:alarm", UNIT_MINUTE),
    "dosing_alarm_redox_threshold": _num("F_DOSAL_MV_MIN", 0, 999, 1, "mdi:alarm", UNIT_MINUTE),
    # allprw: 0 = disabled, 100-250 = active
    "probe_failure_ph_threshold": _num("F_PRBFAIL_PH_MIN", 0, 250, 1, "mdi:alarm-check", UNIT_MINUTE),
    "probe_failure_redox_threshold": _num("F_PRBFAIL_MV_MIN", 0, 250, 1, "mdi:alarm-check", UNIT_MINUTE),
    # flowsw
    "flow_time": _num("F_FLOW_TIME", 0, 99, 1, "mdi:clock-outline", UNIT_MINUTE),
}

CONFIG_SCHEMA = EMEC_PLATFORM_SCHEMA.extend(
    {cv.Optional(key): spec[5] for key, spec in NUMBERS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_EMEC_ID])
    for key, (field, min_value, max_value, step, scale, _) in NUMBERS.items():
        if key not in config:
            continue
        var = await number.new_number(
            config[key], min_value=min_value, max_value=max_value, step=step
        )
        cg.add(var.set_parent(hub, emec_field(field), scale))
        cg.add(hub.register_listener(emec_field(field), var))
