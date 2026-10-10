import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, time as time_
from esphome.const import CONF_TIME_ID, ENTITY_CATEGORY_CONFIG

from .. import CONF_EMEC_ID, EMEC_PLATFORM_SCHEMA, emec_ns

DEPENDENCIES = ["emec"]

EmecResetAlarmsButton = emec_ns.class_("EmecResetAlarmsButton", button.Button)
EmecSyncClockButton = emec_ns.class_("EmecSyncClockButton", button.Button)

CONF_RESET_ALARMS = "reset_alarms"
CONF_SYNC_CLOCK = "sync_clock"

CONFIG_SCHEMA = EMEC_PLATFORM_SCHEMA.extend(
    {
        # Sends `resalw` to clear the latched alarms.
        cv.Optional(CONF_RESET_ALARMS): button.button_schema(
            EmecResetAlarmsButton, icon="mdi:alarm-off"
        ),
        # Pushes the ESP time to the controller (`clockw`). Needs a time source.
        cv.Optional(CONF_SYNC_CLOCK): button.button_schema(
            EmecSyncClockButton,
            icon="mdi:clock-check",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ).extend(
            {
                cv.Required(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
            }
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_EMEC_ID])
    if CONF_RESET_ALARMS in config:
        var = await button.new_button(config[CONF_RESET_ALARMS])
        cg.add(var.set_parent(hub))
    if CONF_SYNC_CLOCK in config:
        conf = config[CONF_SYNC_CLOCK]
        var = await button.new_button(conf)
        cg.add(var.set_parent(hub))
        clock = await cg.get_variable(conf[CONF_TIME_ID])
        cg.add(var.set_time(clock))
