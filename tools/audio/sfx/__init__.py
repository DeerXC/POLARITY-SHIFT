"""POLARITY SHIFT - sound effects, version 9 ("MIRROR"). The command is tools/audio/build_sfx.py; the recipes are in ui.py, ship.py, combat.py, boss.py and flow.py.
Importing this package registers every recipe (registry.SFX)."""
from . import boss, combat, flow, ship, ui  # noqa: F401
from .registry import SFX  # noqa: F401
