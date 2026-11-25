import esphome.codegen as cg
from esphome.components import button, fan, remote_base, select, switch
import esphome.config_validation as cv
from esphome.const import (
    CONF_INTERNAL,
    CONF_NAME,
    CONF_RESTORE_MODE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_AIR_FILTER,
    ICON_BUG,
    ICON_FAN,
    ICON_SIGN_DIRECTION,
)
from esphome.cpp_helpers import register_component, register_parented

AUTO_LOAD = ["remote_base", "fan", "select", "switch", "button"]
DEPENDENCIES = ["remote_transmitter"]
CODEOWNERS = ["@slashrsm"]

ns = cg.esphome_ns.namespace("quantum_next")

QuantumNext = ns.class_(
    "QuantumNext",
    cg.Component,
    remote_base.RemoteReceiverListener,
    remote_base.RemoteTransmittable,
)
QuantumNextFan = ns.class_("QuantumNextFan", fan.Fan, cg.Parented.template(QuantumNext))
QuantumNextSelect = ns.class_(
    "QuantumNextSelect", select.Select, cg.Parented.template(QuantumNext)
)
QuantumNextSwitch = ns.class_(
    "QuantumNextSwitch", switch.Switch, cg.Parented.template(QuantumNext)
)
QuantumNextButton = ns.class_(
    "QuantumNextButton", button.Button, cg.Parented.template(QuantumNext)
)

SelectKind = ns.enum("SelectKind", is_class=True)
SwitchKind = ns.enum("SwitchKind", is_class=True)

CONF_FAN = "fan"
CONF_DIRECTION = "direction"
CONF_MODE = "mode"
CONF_BOOST = "boost"
CONF_FILTER_RESET = "filter_reset"
CONF_DEBUG = "ir_debug"

ICON_BOOST = "mdi:timer-sand"
ICON_MODE = "mdi:tune"

# Select indexes consumed by quantum_next.cpp. Keep these lists in that order.
DIRECTION_OPTIONS = ["Alternate", "Extract", "Intake"]
MODE_OPTIONS = ["Comfort", "Efficiency"]


def _as_entity(value, placeholder):
    """Entity schemas reject a block with neither id nor name. The real name is applied later."""
    value = {} if not isinstance(value, dict) else dict(value)
    value.setdefault(CONF_NAME, placeholder)
    return value


def _validate_fan(value):
    value = _as_entity(value, "Quantum NEXT")
    value.setdefault(CONF_RESTORE_MODE, "NO_RESTORE")
    return fan.fan_schema(QuantumNextFan, icon=ICON_FAN)(value)


def _validate_direction(value):
    return select.select_schema(QuantumNextSelect, icon=ICON_SIGN_DIRECTION)(
        _as_entity(value, "Quantum NEXT Direction")
    )


def _validate_mode(value):
    return select.select_schema(QuantumNextSelect, icon=ICON_MODE)(
        _as_entity(value, "Quantum NEXT Mode")
    )


def _validate_boost(value):
    value = _as_entity(value, "Quantum NEXT Boost")
    value.setdefault(CONF_RESTORE_MODE, "DISABLED")
    return switch.switch_schema(QuantumNextSwitch, icon=ICON_BOOST)(value)


def _validate_debug(value):
    value = _as_entity(value, "Quantum NEXT IR debug")
    value.setdefault(CONF_RESTORE_MODE, "RESTORE_DEFAULT_OFF")
    return switch.switch_schema(
        QuantumNextSwitch,
        icon=ICON_BUG,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )(value)


def _validate_filter_reset(value):
    return button.button_schema(QuantumNextButton, icon=ICON_AIR_FILTER)(
        _as_entity(value, "Quantum NEXT Filter reset")
    )


def _name_entities(config):
    """The block creates the entities. Names follow the component name."""
    name = config[CONF_NAME]
    named = (
        (CONF_FAN, name),
        (CONF_DIRECTION, f"{name} Direction"),
        (CONF_MODE, f"{name} Mode"),
        (CONF_BOOST, f"{name} Boost"),
        (CONF_FILTER_RESET, f"{name} Filter reset"),
        (CONF_DEBUG, f"{name} IR debug"),
    )
    for key, entity_name in named:
        config[key][CONF_NAME] = entity_name
        config[key][CONF_INTERNAL] = False
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(QuantumNext),
            cv.Optional(CONF_NAME, default="Quantum NEXT"): cv.string_strict,
            cv.GenerateID(remote_base.CONF_TRANSMITTER_ID): cv.use_id(
                remote_base.RemoteTransmitterBase
            ),
            cv.Optional(remote_base.CONF_RECEIVER_ID): cv.use_id(
                remote_base.RemoteReceiverBase
            ),
            cv.Optional(CONF_FAN, default={}): _validate_fan,
            cv.Optional(CONF_DIRECTION, default={}): _validate_direction,
            cv.Optional(CONF_MODE, default={}): _validate_mode,
            cv.Optional(CONF_BOOST, default={}): _validate_boost,
            cv.Optional(CONF_FILTER_RESET, default={}): _validate_filter_reset,
            cv.Optional(CONF_DEBUG, default={}): _validate_debug,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _name_entities,
)


async def to_code(config):
    var = cg.new_Pvariable(config[cv.CONF_ID])
    await register_component(var, config)
    await remote_base.register_transmittable(var, config)
    if remote_base.CONF_RECEIVER_ID in config:
        await remote_base.register_listener(var, config)

    fan_var = await fan.new_fan(config[CONF_FAN])
    await register_parented(fan_var, var)
    cg.add(var.set_fan(fan_var))

    direction = await select.new_select(
        config[CONF_DIRECTION], options=DIRECTION_OPTIONS
    )
    await register_parented(direction, var)
    cg.add(direction.set_kind(SelectKind.DIRECTION))
    cg.add(var.set_direction_select(direction))

    mode = await select.new_select(config[CONF_MODE], options=MODE_OPTIONS)
    await register_parented(mode, var)
    cg.add(mode.set_kind(SelectKind.MODE))
    cg.add(var.set_mode_select(mode))

    boost = await switch.new_switch(config[CONF_BOOST])
    await register_parented(boost, var)
    cg.add(boost.set_kind(SwitchKind.BOOST))
    cg.add(var.set_boost_switch(boost))

    debug = await switch.new_switch(config[CONF_DEBUG])
    await register_parented(debug, var)
    cg.add(debug.set_kind(SwitchKind.DEBUG))
    cg.add(var.set_debug_switch(debug))

    reset = await button.new_button(config[CONF_FILTER_RESET])
    await register_parented(reset, var)
