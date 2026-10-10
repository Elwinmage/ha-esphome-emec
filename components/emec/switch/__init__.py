import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import ENTITY_CATEGORY_CONFIG

from .. import CONF_EMEC_ID, EMEC_PLATFORM_SCHEMA, emec_field, emec_ns

DEPENDENCIES = ["emec"]

EmecSwitch = emec_ns.class_("EmecSwitch", switch.Switch)


def _sw():
    return switch.switch_schema(
        EmecSwitch,
        icon="mdi:electric-switch-closed",
        entity_category=ENTITY_CATEGORY_CONFIG,
        # The state always comes from the controller, never from flash.
        default_restore_mode="DISABLED",
    )


# key -> (protocol field, schema). Digital input contact type: ON = N.C.
SWITCHES = {
    "standby_input_nc": ("F_DI_STANDBY_NC", _sw()),
    "ph_level_input_nc": ("F_DI_PH_LEVEL_NC", _sw()),
    "redox_level_input_nc": ("F_DI_MV_LEVEL_NC", _sw()),
}

CONFIG_SCHEMA = EMEC_PLATFORM_SCHEMA.extend(
    {cv.Optional(key): schema for key, (_, schema) in SWITCHES.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_EMEC_ID])
    for key, (field, _) in SWITCHES.items():
        if key not in config:
            continue
        var = await switch.new_switch(config[key])
        cg.add(var.set_parent(hub, emec_field(field)))
        cg.add(hub.register_listener(emec_field(field), var))
