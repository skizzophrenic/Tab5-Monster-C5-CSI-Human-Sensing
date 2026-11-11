// Tab5 Monster C5 CSI Human Sensing -- the SENSING screens (the Tab5 only).
//
// What the second radio on the M5-Bus says about the room, three ways, with
// a swipe between them (dots under the title bar say which):
//
//   SENSING  the everyday screen, in the T-Deck LORA page's language: a
//            status line, a dish with each radio path on it, NOW / FLOOR /
//            PEAK / MARGIN tiles, an 80 s strip, the big state banner, and a
//            tabbed PATHS / EVENTS / RADIO list.
//   SCOPE    a radar: a dome over a vaporwave sun, a sweep with a long
//            phosphor afterglow, a contact on a path's spoke when that path
//            is disturbed, and Squachy at the console.
//   SNEAK    a game: start a run and cross the room without tripping it.
//
// Reached from Settings > SENSING; the detector on the C6 keeps running
// underneath, untouched. tab5_sense.h has the link; docs/SENSING.md says
// what the score can and cannot tell (no distances to people, no counts,
// and a path's spoke is not a compass direction).
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

void uiSensingInit(TFT_eSPI& t);
void uiSensingTick(TFT_eSPI& t, uint32_t now, bool advance = true);

// What a tap asks main.cpp to do. Taps that stay inside the screen (a tab,
// START RUN, LEVEL) are handled by uiSensingTap() itself and return NONE.
enum class SensingTap : uint8_t { NONE, BRIGHT, RECAL, SLEEP };
SensingTap uiSensingTap(int x, int y, int screenW, int screenH, uint32_t now);
// A horizontal swipe: dir -1 = to the previous mode, +1 = the next.
void uiSensingSwipe(int dir);
// Which mode (0 SENSING, 1 SCOPE, 2 SNEAK), for the console and the emulator.
uint8_t uiSensingMode();
// The VITALS mode is the one showing: the radio's chest signal is wanted.
bool uiSensingIsVitals();
void uiSensingSetMode(uint8_t m);

