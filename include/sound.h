// Tab5 Monster C5 CSI Human Sensing -- sounds. The Tab5 build has none: the
// calls Squachy's drawing code makes are no-ops.
#pragma once
#include <stdint.h>

namespace Sound {
    enum class Id : uint8_t { NEW_DEVICE = 0, WATCH_HIT, LOW_BATTERY, MESSAGE, VISITOR, THROW, LAND,
                              CLIPPY, TOAST_POP, PLUG_IN, UNPLUG, LAND_TOASTER, LAND_YETI, HEADS_UP, COUNT };
    inline void begin() {}
    inline void play(Id, bool = false) {}
}
