// Tab5 Monster C5 CSI Human Sensing -- the Tab5's link to a sensing radio on the M5-Bus.
// See include/tab5_sense.h.
#if defined(TAB5)
#include <Arduino.h>
#include <HardwareSerial.h>
#include <string.h>
#include "tab5_sense.h"
#include <esp_heap_caps.h>

namespace Tab5Sense {

static bool        s_up = false;
static bool        s_heard = false;
static SenseReport s_last = {};
static uint32_t    s_lastAt = 0;
static uint32_t    s_motionAt = 0;
static bool        s_motionSeen = false;
static uint32_t    s_lines = 0, s_bad = 0;
static char        s_info[96] = "";
static char        s_buf[SENSE_LINE_MAX + 32];
static uint16_t    s_len = 0;

// The history ring: written at the radio's line rate.
static uint8_t     s_score[HISTORY];
static uint8_t     s_state[HISTORY];
static uint16_t    s_live[HISTORY];
static uint8_t     s_pres[(HISTORY + 7) / 8];
static uint16_t    s_head = 0, s_count = 0;

static Path        s_path[PATHS];
static uint32_t    s_pathAt[PATHS];

static VFrame*     s_v = nullptr;     // VHISTORY frames, in PSRAM (19 KB the internal heap can't spare)
static uint32_t    s_vTotal = 0, s_vAt = 0;

static Event       s_ev[EVENTS];
static uint8_t     s_evHead = 0, s_evCount = 0;
static bool        s_inMotion = false;

static void reset() {
    s_heard = false; s_motionSeen = false; s_inMotion = false;
    s_len = 0; s_lines = s_bad = 0;
    s_head = s_count = 0; s_evHead = s_evCount = 0;
    s_vTotal = 0; s_vAt = 0;
    s_info[0] = 0;
    memset(&s_last, 0, sizeof s_last);
    memset(s_path, 0, sizeof s_path);
    memset(s_pathAt, 0, sizeof s_pathAt);
}

static uint32_t s_holdUntil = 0;
void hold(uint32_t ms) { s_holdUntil = millis() + ms; }
bool held(uint32_t now) { return (int32_t)(s_holdUntil - now) > 0; }

bool begin() {
    if (s_up) return true;
    // Room for a stall: ten $S lines a second is ~1 KB/s, and VITALS adds
    // twenty $V lines (~0.6 KB/s). 4 KB is over two seconds of slack.
    Serial1.setRxBufferSize(4096);
    Serial1.begin(SENSE_BAUD, SERIAL_8N1, SENSE_TAB5_RX, SENSE_TAB5_TX);
    s_up = true;
    reset();
    Serial.printf("[sense] UART1 open on the M5-Bus: RX %d, TX %d, %d baud\n", SENSE_TAB5_RX, SENSE_TAB5_TX, SENSE_BAUD);
    send("info");
    // VITALS listens rather than joining a network (joined, the radio stopped
    // taking commands on the bench, 2026-11-08): clear any VITALS network a
    // radio was given, so "vitals on" always means listening.
    send("vitalsnet forget");
    return true;
}

void end() {
    if (!s_up) return;
    // Let a last command (VITALS' "vitals off") finish leaving first: ending
    // straight after sending cut it to "vi" and left the radio joined.
    Serial1.flush();
    delay(5);
    Serial1.end();
    // Let go of the bus pin we were driving: as a plain input it no longer
    // holds the Monster's RX line high, so a PC on the Monster's own USB-C
    // can reach it (flashing it in place with flash_known.py --download).
    // Left as it was, the P4's UART0 console owns the pin again and the
    // PC's bytes never arrive (2026-11-07).
    // (2026-11-08: resetting the pad with gpio_reset_pin instead made the
    // P4's console come out on the bus once UART1 reopened, and the radio
    // stopped hearing our commands. pinMode is what works.)
    pinMode(SENSE_TAB5_TX, INPUT);
    s_up = false;
    s_heard = false;
    Serial.printf("[sense] UART1 closed after %lu lines (%lu bad)\n", (unsigned long)s_lines, (unsigned long)s_bad);
}

bool up() { return s_up; }

static void push(const SenseReport& r) {
    s_score[s_head] = r.score;
    s_state[s_head] = r.state;
    s_live[s_head] = r.live;
    const uint8_t bit = (uint8_t)(1u << (s_head & 7));
    if (r.presence) s_pres[s_head >> 3] |= bit; else s_pres[s_head >> 3] &= (uint8_t)~bit;
    s_head = (uint16_t)((s_head + 1) % HISTORY);
    if (s_count < HISTORY) s_count++;
}

// A MOTION stretch opens an event and closing it records how long it ran.
static void trackEvents(const SenseReport& r, uint32_t now) {
    const bool m = r.state == SENSE_STATE_MOTION;
    if (m && !s_inMotion) {
        s_ev[s_evHead] = { now, 0, true };
        s_evHead = (uint8_t)((s_evHead + 1) % EVENTS);
        if (s_evCount < EVENTS) s_evCount++;
    }
    if (s_evCount) {
        Event& e = s_ev[(s_evHead + EVENTS - 1) % EVENTS];
        if (e.open) { e.ms = now - e.at; if (!m) e.open = false; }
    }
    s_inMotion = m;
}

static void onLine(const char* line, uint32_t now) {
    if (line[0] == '$' && line[1] == 'S') {
        SenseReport r;
        if (senseParseLine(line, r)) {
            s_last = r; s_lastAt = now; s_heard = true; s_lines++;
            if (r.state == SENSE_STATE_MOTION) { s_motionAt = now; s_motionSeen = true; }
            push(r);
            trackEvents(r, now);
        } else {
            s_bad++;
        }
    } else if (line[0] == '$' && line[1] == 'T') {
        SenseTarget t;
        if (senseParseTarget(line, t) && t.idx < PATHS) {
            Path& p = s_path[t.idx];
            if (p.tag != t.tag || !p.live) {          // a new transmitter in this slot
                memset(&p, 0, sizeof p);
                for (int k = 0; k < 4; k++) p.trail[k] = t.rssi;
            } else {
                memmove(p.trail + 1, p.trail, 3);
                p.trail[0] = t.rssi;
            }
            p.live = true; p.tag = t.tag; p.rssi = t.rssi; p.rate = t.rate;
            p.score = t.score; p.cal = t.cal; p.rel = t.rel;
            if (t.score >= 28 && t.cal >= 100) { p.hotAt = now; p.everHot = true; }
            s_pathAt[t.idx] = now;
        } else {
            s_bad++;
        }
    } else if (line[0] == '$' && line[1] == 'V') {
        SenseVitals v;
        if (senseParseVitals(line, v)) {
            if (!s_v) s_v = (VFrame*)heap_caps_calloc(VHISTORY, sizeof(VFrame), MALLOC_CAP_SPIRAM);
            if (!s_v) return;
            VFrame& f = s_v[s_vTotal % VHISTORY];
            f.c1 = v.c1 / 10000.0f; f.c2 = v.c2 / 10000.0f; f.n = v.n; f.q = v.q; f.at = now;
            s_vTotal++; s_vAt = now;
        } else {
            s_bad++;
        }
    } else if (line[0] == '$' && line[1] == 'I' && line[2] == ',') {
        strncpy(s_info, line + 3, sizeof s_info - 1);
        s_info[sizeof s_info - 1] = 0;
        Serial.printf("[sense] radio: %s\n", s_info);
    } else if (line[0] == '#') {
        // The radio's notes for a human: onto our console, so a bench log
        // has both halves in one place.
        Serial.printf("[sense] %s\n", line);
    }
}

void tick(uint32_t now) {
    if (!s_up) return;
    // One console line every ten seconds while on: the link's health and the
    // radio's last word, for a bench log or a soak without the screen open.
    static uint32_t s_statAt = 0, s_statLines = 0;
    if (now - s_statAt >= 10000) {
        const uint32_t dl = s_lines - s_statLines;
        Serial.printf("[sense] lines %lu (+%lu in %lus) bad %lu  %s  score %u state %u pres %u  rate %u/s  %s GHz ch %u  rssi %d  paths %u  heap %lu\n",
                      (unsigned long)s_lines, (unsigned long)dl, (unsigned long)((now - s_statAt) / 1000), (unsigned long)s_bad,
                      fresh(now) ? "FRESH" : (s_heard ? "STALE" : "NOTHING YET"),
                      (unsigned)s_last.score, (unsigned)s_last.state, (unsigned)s_last.presence, (unsigned)s_last.rate,
                      s_last.band == 5 ? "5" : "2.4", (unsigned)s_last.chan, (int)s_last.rssi, (unsigned)livePaths(now),
                      (unsigned long)ESP.getFreeHeap());
        s_statAt = now; s_statLines = s_lines;
    }
    // A slot that has gone quiet is a transmitter the radio dropped.
    for (uint8_t k = 0; k < PATHS; k++)
        if (s_path[k].live && now - s_pathAt[k] > 5000) s_path[k].live = false;
    // Bounded per call, so a flood (or a radio stuck printing) can never
    // hold the frame up: 2 KB is two seconds of lines.
    int budget = 4096;
    while (budget-- > 0 && Serial1.available()) {
        const char c = (char)Serial1.read();
        if (c == '\n' || c == '\r') {
            if (s_len) { s_buf[s_len] = 0; onLine(s_buf, now); s_len = 0; }
        } else if (s_len < sizeof s_buf - 1) {
            s_buf[s_len++] = c;
        } else {
            s_len = 0;   // a line too long is thrown away
        }
    }
}

bool fresh(uint32_t now) { return s_heard && now - s_lastAt < 2000; }
bool heard() { return s_heard; }
const SenseReport& last() { return s_last; }
uint32_t lastAt() { return s_lastAt; }
uint32_t sinceMotion(uint32_t now) { return s_motionSeen ? now - s_motionAt : UINT32_MAX; }

uint16_t count() { return s_count; }
static uint16_t slot(uint16_t i) { return (uint16_t)((s_head + HISTORY - s_count + i) % HISTORY); }
uint8_t  scoreAt(uint16_t i)    { return s_score[slot(i)]; }
uint8_t  stateAt(uint16_t i)    { return s_state[slot(i)]; }
uint16_t liveAt(uint16_t i)     { return s_live[slot(i)]; }
bool     presenceAt(uint16_t i) { const uint16_t k = slot(i); return (s_pres[k >> 3] >> (k & 7)) & 1; }

const Path& path(uint8_t idx) { return s_path[idx < PATHS ? idx : 0]; }
uint8_t livePaths(uint32_t now) {
    uint8_t n = 0;
    for (uint8_t k = 0; k < PATHS; k++) if (s_path[k].live && now - s_pathAt[k] <= 5000) n++;
    return n;
}

uint8_t events(Event* out, uint8_t cap) {
    uint8_t n = 0;
    for (uint8_t k = 0; k < s_evCount && n < cap; k++) out[n++] = s_ev[(s_evHead + EVENTS - 1 - k) % EVENTS];
    return n;
}

bool send(const char* cmd) {
    if (!s_up || !cmd) return false;
    Serial1.print(cmd);
    Serial1.print('\n');
    return true;
}

uint32_t vTotal() { return s_vTotal; }
bool vGet(uint32_t k, VFrame& out) {
    if (!s_v || k >= s_vTotal || s_vTotal - k > VHISTORY) return false;
    out = s_v[k % VHISTORY];
    return true;
}
uint32_t vLastAt() { return s_vAt; }

const char* info() { return s_info; }
uint32_t lines() { return s_lines; }
uint32_t badLines() { return s_bad; }

}  // namespace Tab5Sense
#endif  // TAB5
