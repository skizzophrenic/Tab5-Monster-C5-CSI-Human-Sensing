// Tab5 Monster C5 CSI Human Sensing -- the Tab5 app.
//
// The M5MonsterC5 on the Tab5's M5-Bus listens to the WiFi in the room and
// sends a few numbers ten times a second (include/sense_proto.h). This draws
// them: four screens a swipe apart, SENSING, SCOPE, SNEAK and VITALS.
//
// Frames are drawn into a 640x360 8-bit sprite and Tab5Blit doubles them onto
// the 1280x720 panel. The Tab5's own radio (the C6) is never powered.
//
// Buttons along the bottom: BRIGHT steps the screen down (all the way to
// barely lit, for filming) and wraps back to full; RECAL recalibrates the
// radio; SLEEP turns the screen off until it is touched. The middle button
// is START RUN on SNEAK and NEW SESSION on VITALS.
//
// Console (115200): `cal` and any other radio command is passed straight to
// the radio; `pass [s]` lets go of the M5-Bus for that long (90 s by default)
// so the Monster can be reached on its own USB.
#include <Arduino.h>
#include "tab5_board.h"
#include "tab5_display.h"
#include "tab5_panel.h"
#include "tab5_blit.h"
#include "tab5_sense.h"
#include "fast_sprite.h"
#include "settings.h"
#include "theme.h"
#include "ui_sensing.h"
#include "vitals.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif

static Tab5TFT    tft;
static FastSprite frame(&tft);
static bool       s_frameOk = false;

// A short message over the bottom of the screen: the brightness, RECAL.
static char     s_toast[40] = "";
static uint32_t s_toastAt = 0;
static uint16_t s_toastColor = 0;
static void toast(const char* msg, uint16_t color) {
    snprintf(s_toast, sizeof s_toast, "%s", msg);
    s_toastAt = millis();
    s_toastColor = color;
}
static void drawToast(TFT_eSPI& t, uint32_t now) {
    if (!s_toast[0] || now - s_toastAt > 1800) return;
    const Theme::BangersSize sz = Theme::BangersSize::MD;
    const int w = Theme::bangersTextWidth(s_toast, sz) + 32, h = 34;
    const int x = (t.width() - w) / 2, y = t.height() - 84;
    t.fillRoundRect(x, y, w, h, 6, Theme::BG);
    t.drawRoundRect(x, y, w, h, 6, s_toastColor);
    Theme::drawBangersText(t, x + 16, y + 6, s_toast, s_toastColor, sz);
}

static bool s_asleep = false;
static void applyBrightness() { Tab5Panel::backlight(s_asleep ? 0 : Settings::brightness()); }

static void stepBrightness() {
    const uint8_t b = Settings::nextBrightness(Settings::brightness());
    Settings::setBrightness(b);
    applyBrightness();
    char m[32];
    const uint32_t tenths = (uint32_t)b * 1000 / 255;
    if (tenths >= 100) snprintf(m, sizeof m, "BRIGHTNESS %lu%%", (unsigned long)((tenths + 5) / 10));
    else snprintf(m, sizeof m, "BRIGHTNESS %lu.%lu%%", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
    toast(m, Theme::CYAN);
}

// The console: radio commands pass through; `pass [s]` frees the bus.
static void console(uint32_t now) {
    static char line[96];
    static uint8_t n = 0;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n') { if (n < sizeof line - 1) line[n++] = c; continue; }
        line[n] = 0; n = 0;
        if (!line[0]) continue;
        if (!strncmp(line, "pass", 4) && (line[4] == 0 || line[4] == ' ')) {
            const long s = line[4] ? atol(line + 5) : 90;
            Tab5Sense::hold((uint32_t)(s > 0 ? s : 90) * 1000UL);
            Serial.printf("[sense] letting go of the M5-Bus for %ld s\n", s > 0 ? s : 90);
        } else if (Tab5Sense::up()) {
            Tab5Sense::send(line);
            Serial.printf("[sense] sent: %s\n", line);
        } else {
            Serial.println("[sense] the radio link is not open");
        }
    }
    (void)now;
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(200);
    Serial.printf("\nTab5 Monster C5 CSI Human Sensing %s\n", FIRMWARE_VERSION);
    Settings::begin();
    tft.init();                       // the panel, through M5GFX
    Tab5Panel::backlight(24);         // low while the radio link comes up
    Theme::setLarge(true);
    frame.setColorDepth(8);
    s_frameOk = frame.createSprite(TAB5_LOGICAL_W, TAB5_LOGICAL_H) != nullptr;
    if (!s_frameOk) Serial.println("[tab5] no memory for the frame");
    uiSensingInit(frame);
    applyBrightness();
    Serial.printf("[tab5] brightness %u, mode %u\n", Settings::brightness(), Settings::sensingMode());
}

void loop() {
    const uint32_t now = millis();
    console(now);

    // The radio link: open whenever the bus is not lent out.
    if (!Tab5Sense::held(now)) {
        if (!Tab5Sense::up()) Tab5Sense::begin();
        Tab5Sense::tick(now);
        Vitals::service(now, uiSensingIsVitals());
        Vitals::update(now);
    } else if (Tab5Sense::up()) {
        Vitals::service(now, false);
        Tab5Sense::end();
    }

    // Touch: a sideways drag of 60 px or more is a swipe to the next or
    // previous screen; anything else is a tap where the finger went down,
    // acted on at release so a swipe that starts on a button does not press it.
    static bool down = false;
    static int dx0 = 0, dy0 = 0, lx = 0, ly = 0;
    static uint32_t lastTouch = 0;
    uint16_t px, py;
    const bool touching = Tab5Panel::hasTouch() && Tab5Touch::read(px, py);
    if (touching) {
        const int x = px / TAB5_SCALE, y = py / TAB5_SCALE;
        if (!down) { down = true; dx0 = lx = x; dy0 = ly = y; }
        else { lx = x; ly = y; }
    } else if (down) {
        down = false;
        if (s_asleep) {
            s_asleep = false;
            applyBrightness();
        } else if (now - lastTouch > 200) {
            const int dx = lx - dx0, dy = ly - dy0;
            if (dx * dx > 60 * 60 && dx * dx > 2 * dy * dy) {
                uiSensingSwipe(dx < 0 ? 1 : -1);
            } else {
                const SensingTap tap = uiSensingTap(dx0, dy0, TAB5_LOGICAL_W, TAB5_LOGICAL_H, now);
                if (tap == SensingTap::BRIGHT) stepBrightness();
                else if (tap == SensingTap::RECAL) {
                    if (Tab5Sense::send("cal")) toast("RECALIBRATING: KEEP STILL 30 S", Theme::AMBER);
                } else if (tap == SensingTap::SLEEP) {
                    s_asleep = true;
                    applyBrightness();
                }
            }
            lastTouch = now;
        }
    }

    if (!s_frameOk || s_asleep) { delay(20); return; }
    uiSensingTick(frame, now, true);
    drawToast(frame, now);
    Tab5Blit::push(frame.buf(), frame.bufW(), frame.bufH(), 0, 0);
}
