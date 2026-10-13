import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ADDRESS, CONF_ID

CODEOWNERS = ["@Elwinmage"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True

emec_ns = cg.esphome_ns.namespace("emec")
EmecComponent = emec_ns.class_("EmecComponent", cg.PollingComponent, uart.UARTDevice)

# Referenced by every platform (sensor, binary_sensor, number, select, ...).
CONF_EMEC_ID = "emec_id"

CONF_PREFIX = "prefix"
CONF_HEARTBEAT_INTERVAL = "heartbeat_interval"
CONF_STARTUP_DELAY = "startup_delay"
CONF_TAU = "tau"
CONF_PH_FILTER = "ph_filter"
CONF_REDOX_FILTER = "redox_filter"
CONF_THRESHOLD = "threshold"
CONF_MAX_OUTLIERS = "max_outliers"


def _filter_schema(threshold_validator, default_threshold):
    # threshold: max jump vs the last normal reading (0 = filter disabled).
    # max_outliers: consecutive off readings dropped before raising the alarm.
    return cv.Schema(
        {
            cv.Optional(CONF_THRESHOLD, default=default_threshold): threshold_validator,
            cv.Optional(CONF_MAX_OUTLIERS, default=3): cv.int_range(min=1, max=20),
        }
    )


PH_FILTER_SCHEMA = _filter_schema(cv.float_range(min=0.0, max=14.0), 1.0)
REDOX_FILTER_SCHEMA = _filter_schema(cv.int_range(min=0, max=2000), 50)

# Shared by the platforms: which hub the entities belong to.
EMEC_PLATFORM_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_EMEC_ID): cv.use_id(EmecComponent),
    }
)


def emec_field(name):
    """C++ expression for a protocol field (see emec_protocol.h)."""
    return cg.RawExpression(f"emec_core::{name}")


def _prefix(value):
    value = cv.string_strict(value)
    if len(value) != 2 or not value.isalnum():
        raise cv.Invalid("prefix must be exactly 2 alphanumeric characters")
    return value


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(EmecComponent),
            # RS-485 slave address of the controller (01 from factory).
            cv.Optional(CONF_ADDRESS, default=1): cv.int_range(min=1, max=99),
            # Protocol family prefix; "34" on every WDPHRH seen so far.
            cv.Optional(CONF_PREFIX, default="34"): _prefix,
            # Keepalive period. The pump goes silent without it.
            cv.Optional(
                CONF_HEARTBEAT_INTERVAL, default="500ms"
            ): cv.positive_time_period_milliseconds,
            # Heartbeat-only period after boot, before the first query.
            cv.Optional(
                CONF_STARTUP_DELAY, default="2s"
            ): cv.positive_time_period_milliseconds,
            # Trailing field of the `paramw` write. It cannot be read back, so
            # this value is sent with every passcode / feeding delay / priority
            # change.
            cv.Optional(CONF_TAU, default=0): cv.int_range(min=0, max=99),
            # Spike filters on the live readings, enabled by default.
            cv.Optional(CONF_PH_FILTER, default={}): PH_FILTER_SCHEMA,
            cv.Optional(CONF_REDOX_FILTER, default={}): REDOX_FILTER_SCHEMA,
        }
    )
    .extend(cv.polling_component_schema("10s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "emec",
    baud_rate=38400,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="NONE",
    stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_prefix(config[CONF_PREFIX]))
    cg.add(var.set_heartbeat_interval(config[CONF_HEARTBEAT_INTERVAL].total_milliseconds))
    cg.add(var.set_startup_delay(config[CONF_STARTUP_DELAY].total_milliseconds))
    cg.add(var.set_tau(config[CONF_TAU]))
    ph = config[CONF_PH_FILTER]
    cg.add(var.set_ph_filter(int(round(ph[CONF_THRESHOLD] * 100)), ph[CONF_MAX_OUTLIERS]))
    mv = config[CONF_REDOX_FILTER]
    cg.add(var.set_redox_filter(mv[CONF_THRESHOLD], mv[CONF_MAX_OUTLIERS]))
