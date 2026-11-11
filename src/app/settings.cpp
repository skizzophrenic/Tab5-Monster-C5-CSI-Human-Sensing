// Tab5 Monster C5 CSI Human Sensing -- the saved settings. See settings.h.
#include "settings.h"
#include <Preferences.h>

namespace Settings {

static Preferences s_prefs;
static bool    s_open = false;
static uint8_t s_bright = 128;
static uint8_t s_mode = 0;

void begin() {
    s_open = s_prefs.begin("csisense", false);
    if (!s_open) return;
    s_bright = s_prefs.getUChar("bright", 128);
    if (s_bright == 0) s_bright = 1;
    s_mode = s_prefs.getUChar("mode", 0);
    if (s_mode > 3) s_mode = 0;
}

uint8_t brightness() { return s_bright; }
void setBrightness(uint8_t v) {
    if (v == 0) v = 1;
    if (v == s_bright) return;
    s_bright = v;
    if (s_open) s_prefs.putUChar("bright", v);
}

uint8_t sensingMode() { return s_mode; }
void setSensingMode(uint8_t m) {
    if (m > 3 || m == s_mode) return;
    s_mode = m;
    if (s_open) s_prefs.putUChar("mode", m);
}

// 255, 192, 128, 64, 32, then halving to 1, then back to the top.
uint8_t nextBrightness(uint8_t now) {
    if (now > 192) return 192;
    if (now > 128) return 128;
    if (now > 64)  return 64;
    if (now > 32)  return 32;
    if (now > 1)   return (uint8_t)(now / 2);
    return 255;
}

}  // namespace Settings
