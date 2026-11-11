// Tab5 Monster C5 CSI Human Sensing -- the Tab5's link to a sensing radio on the M5-Bus.
//
// The M5MonsterC5 (C5Lab's ESP32-C5 add-on) plugs into the Tab5's long rear
// connector and, running src/sense_c5/, watches the WiFi channel's shape and
// prints one ASCII status line ten times a second, plus one line per
// transmitter it models once a second (include/sense_proto.h). This module
// owns the UART on the bus pins, reads those lines, and keeps what the
// SENSING screens draw from: the last report, the last 80 s of scores, each
// transmitter's path, and a log of movements. It sends the screens' commands
// (recalibrate) back.
//
// It never touches the C6, the SDIO link or the detector: the Tab5's own
// radios keep sniffing exactly as they do with sensing off.
//
// Why UART1 and not Serial0: pins 37/38 are also the P4's UART0, which is
// the IDF console (boot log, ESP_LOG). Routing UART1 to them takes the pins
// over, so nothing but our lines reaches the radio while sensing is on.
// C5Lab's own Tab5 app talks to JanOS the same way (its UART2 on 37/38).
//
// Off by default (Settings::sensing()): the UART is opened only while on.
#pragma once
#include <stdint.h>
#include "sense_proto.h"

namespace Tab5Sense {

static const uint16_t HISTORY = 800;   // the last 80 s, at the radio's 10 lines a second
static const uint8_t  PATHS   = 4;     // transmitters the radio models at once
static const uint8_t  EVENTS  = 12;    // movements remembered

bool begin();                 // open the UART on the bus pins; true once open
// Let go of the bus for ms (the SENSE PASS console command): the loop closes
// the UART and leaves it closed until the time is up, so a PC on the Monster's
// own USB can talk to it (flash it) without anyone tapping OFF.
void hold(uint32_t ms);
bool held(uint32_t now);
void end();                   // close it and forget the radio
bool up();                    // the UART is open
void tick(uint32_t now);      // read whatever arrived; cheap, call every loop

// A $S line arrived within the last two seconds: the radio is there and running.
bool fresh(uint32_t now);
// Ever heard one since begin().
bool heard();
const SenseReport& last();
uint32_t lastAt();            // millis() of the last good $S line
// How long since a line last said MOTION, in ms; UINT32_MAX if none has.
// PRESENCE outlasts MOTION by the radio's hold time, and the screen says so.
uint32_t sinceMotion(uint32_t now);

// The history, oldest first: i in [0, count()).
uint16_t count();
uint8_t  scoreAt(uint16_t i);
uint8_t  stateAt(uint16_t i);  // SENSE_STATE_*
uint16_t liveAt(uint16_t i);   // 0-1000
bool     presenceAt(uint16_t i);

// One transmitter's path through the room, from its $T lines. The radio
// does not know where the transmitter is: a path's score says THAT its path
// was disturbed, not which way it lies (docs/SENSING.md).
struct Path {
    bool     live;            // a $T line for this slot in the last 5 s
    uint16_t tag;             // last two bytes of its address: stable, never shown
    int8_t   rssi;
    uint16_t rate;
    uint8_t  score, cal, rel;
    uint32_t hotAt;           // millis() its score last stood at or above the MOTION line
    bool     everHot;
    int8_t   trail[4];        // its last four RSSI readings, newest first
};
const Path& path(uint8_t idx);  // idx < PATHS
uint8_t livePaths(uint32_t now);

// Movements: a MOTION stretch from the radio, newest first.
struct Event { uint32_t at; uint32_t ms; bool open; };
uint8_t events(Event* out, uint8_t cap);

// The chest signal ($V lines, 20 a second while VITALS is on): frames are
// numbered from begin(), and the last VHISTORY are kept, so a reader can pick
// up wherever it left off. src/vitals.cpp does the maths on them.
static const uint16_t VHISTORY = 1200;   // 60 s
struct VFrame { float c1, c2; uint8_t n, q; uint32_t at; };
uint32_t vTotal();                       // frames since begin()
bool     vGet(uint32_t k, VFrame& out);  // frame k (0-based); false once it has left the ring
uint32_t vLastAt();                      // millis() of the last $V line, 0 if none

// One command line to the radio ("cal", "park 48", ...). False when closed.
bool send(const char* cmd);

// The radio's last $I line, without the "$I," (firmware, MAC, mode, ...), or "".
const char* info();
uint32_t lines();             // good $S lines since begin()
uint32_t badLines();          // lines that started with $S or $T but did not parse

}  // namespace Tab5Sense
