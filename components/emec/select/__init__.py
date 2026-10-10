import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import ENTITY_CATEGORY_CONFIG

from .. import CONF_EMEC_ID, EMEC_PLATFORM_SCHEMA, emec_field, emec_ns

DEPENDENCIES = ["emec"]

EmecSelect = emec_ns.class_("EmecSelect", select.Select)

WORKING_MODE = {"PROPORTIONAL": 0, "ON/OFF": 1}
ALARM_MODE = {"DOSE": 0, "STOP": 1}


def _sel(field, icon, options):
    return (
        field,
        options,
        select.select_schema(
            EmecSelect, icon=icon, entity_category=ENTITY_CATEGORY_CONFIG
        ),
    )


# key -> (protocol field, {label: on-wire value}, schema)
SELECTS = {
    "ph_working_mode": _sel("F_PH_MODE", "mdi:swap-horizontal", WORKING_MODE),
    "redox_working_mode": _sel("F_MV_MODE", "mdi:swap-horizontal", WORKING_MODE),
    "priority_mode": _sel(
        "F_PRIORITY",
        "mdi:priority-high",
        {"No priority": 1, "pH priority": 2, "Redox priority": 3},
    ),
    "flow_mode": _sel("F_FLOW_MODE", "mdi:waves", {"Disable": 0, "Reverse": 1, "Direct": 2}),
    "probe_failure_ph_mode": _sel("F_PRBFAIL_PH_MODE", "mdi:alarm-check", ALARM_MODE),
    "probe_failure_redox_mode": _sel("F_PRBFAIL_MV_MODE", "mdi:alarm-check", ALARM_MODE),
    "dosing_alarm_ph_mode": _sel("F_DOSAL_PH_MODE", "mdi:alarm", ALARM_MODE),
    "dosing_alarm_redox_mode": _sel("F_DOSAL_MV_MODE", "mdi:alarm", ALARM_MODE),
}

CONFIG_SCHEMA = EMEC_PLATFORM_SCHEMA.extend(
    {cv.Optional(key): spec[2] for key, spec in SELECTS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_EMEC_ID])
    for key, (field, options, _) in SELECTS.items():
        if key not in config:
            continue
        var = await select.new_select(config[key], options=list(options.keys()))
        cg.add(var.set_parent(hub, emec_field(field)))
        cg.add(var.set_wire_values(list(options.values())))
        cg.add(hub.register_listener(emec_field(field), var))
