// Tab5 Monster C5 CSI Human Sensing -- the few things the app remembers.
//
// Brightness and the last sensing mode are saved (NVS, namespace "csisense").
// The rest are fixed answers to questions Squachy's drawing code asks: he came
// from a bigger app with backgrounds, fonts and banter settings, and here
// they always have their default value.
#pragma once
#include <stdint.h>

namespace Settings {
    // Saved.
    void    begin();
    uint8_t brightness();              // 1..255, 255 brightest
    void    setBrightness(uint8_t v);
    uint8_t sensingMode();             // 0 SENSING, 1 SCOPE, 2 SNEAK, 3 VITALS
    void    setSensingMode(uint8_t m);

    // The brightness steps the BRIGHT button walks through, brightest first.
    // Below 32 each step halves, so the panel can be turned right down for
    // filming in a dark room.
    uint8_t nextBrightness(uint8_t now);

    // Fixed: what Squachy's drawing code asks about.
    enum class Background : uint8_t {
        DIGITAL = 0, STARFIELD = 1, TOASTERS = 2,
        AQUARIUM = 3, TERMINAL = 4, FIREFLIES = 5,
        FIRE = 6, SNOWFALL = 7, SPECTRUM = 8,
        TUNNEL = 9, SYNTHWAVE = 10, BLACK = 11
    };
    static const uint8_t BACKGROUND_COUNT = 12;
    inline Background background() { return Background::SYNTHWAVE; }
    inline bool    rotationLocked() { return true; }
    inline bool    auraShown()      { return false; }
    enum class FontKind : uint8_t { MENUS = 0, TEXT, BUTTONS, BUBBLES, XP_TILES, PETS, COUNT };
    inline uint8_t fontStep(FontKind) { return 0; }
    inline void    setFontStep(FontKind, uint8_t) {}
    inline uint8_t fontFace(FontKind) { return 0; }
    inline void    setFontFace(FontKind, uint8_t) {}
    inline uint8_t banter()         { return 1; }
    inline float   banterScale()    { return 1.0f; }
    inline uint8_t mascotTempoPct() { return 100; }
    enum class Hunt : uint8_t { EYE_STREAK = 0, COUNT };
    inline uint8_t huntProgress(Hunt) { return 0; }
    inline void    setHuntProgress(Hunt, uint8_t) {}
}
