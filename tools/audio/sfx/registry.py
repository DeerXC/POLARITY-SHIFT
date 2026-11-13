"""The table of sound effects: a recipe registers itself with @sfx(name, class, variants) and build_sfx.py renders the table."""

SFX = {}


def sfx(name, cls, variants=1):
    """Register a recipe. `cls` is the class of the sound (build_sfx.LEVELS: its loudness and the length of its tail), `variants` how many files it has
    (name.wav, name_2.wav ...): the engine plays one of them at random, never the same twice in a row. The recipe is fn(rng, variant) -> mono float array."""

    def decorator(fn):
        if name in SFX:
            raise ValueError(f"the sound '{name}' is registered twice")
        SFX[name] = (fn, cls, variants)
        return fn

    return decorator
