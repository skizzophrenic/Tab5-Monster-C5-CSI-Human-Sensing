// Tab5 Monster C5 CSI Human Sensing -- colours, buttons, the Bangers headline
// font and Squachy's speech bubble text. Taken from the larger app this
// started in; only what the four sensing screens and Squachy use.
#include "theme.h"
#include "bangers_font.h"
#include "settings.h"

namespace Theme {

// His bubble's face: TFT_eSPI's font 2, the built-in 16-row proportional one.
void bubbleFontOn(TFT_eSPI& t)  { t.setTextFont(2); }
void bubbleFontOff(TFT_eSPI& t) { t.setTextFont(1); }
int  bubbleTextH()  { return 16; }
int  bubbleAscent() { return 0; }

// Default-initialized to the original vaporwave vaporwave values —
// applyPalette(0) (VAPRW4VE) reproduces these exactly.
uint16_t BG           = 0x0801;

uint16_t TASKBAR      = 0x0803;

uint16_t PURPLE       = 0xAC1F;

uint16_t CYAN         = 0x07FF;

uint16_t PINK         = 0xF96F;

uint16_t VAPOR_PINK   = 0xFB99;

uint16_t VAPOR_PURPLE = 0xBB5F;

uint16_t VAPOR_BLUE   = 0x067F;

uint16_t VAPOR_YELLOW = 0xFFD2;

uint16_t GREEN        = 0x07E0;

uint16_t AMBER        = 0xFD20;

uint16_t RED          = 0xF800;

uint16_t labelOn(uint16_t fill) {
    const int r = (fill >> 11) & 0x1F, g = (fill >> 5) & 0x3F, b = fill & 0x1F;
    // Perceived brightness, 0..~255, with the 5/6/5 channels scaled to 8 bits.
    const int luma = (r * 8 * 54 + g * 4 * 183 + b * 8 * 19) >> 8;
    return luma > 120 ? BLACK : WHITE;
}

uint16_t blend(uint16_t a, uint16_t b, uint16_t t) {
    // 8.8 fixed-point t, 0..256
    uint8_t ar = (a >> 8) & 0xF8;
    uint8_t ag = (a >> 3) & 0xFC;
    uint8_t ab = (a << 3) & 0xF8;
    uint8_t br = (b >> 8) & 0xF8;
    uint8_t bg = (b >> 3) & 0xFC;
    uint8_t bb = (b << 3) & 0xF8;
    uint8_t rr = (uint8_t)(((uint16_t)ar * (256 - t) + (uint16_t)br * t) >> 8) & 0xF8;
    uint8_t rg = (uint8_t)(((uint16_t)ag * (256 - t) + (uint16_t)bg * t) >> 8) & 0xFC;
    uint8_t rb = (uint8_t)(((uint16_t)ab * (256 - t) + (uint16_t)bb * t) >> 8) & 0xF8;
    return (uint16_t)((rr << 8) | (rg << 3) | (rb >> 3));
}

static bool s_compact = false;

void setCompact(bool on) { s_compact = on; }

bool compact()           { return s_compact; }

static bool s_large = false;

void setLarge(bool on)   { s_large = on; }

bool large()             { return s_large; }

uint8_t uiTextSize(TFT_eSPI& t, uint8_t base) {
    // The large panels in portrait too: 320 wide, but on 3.5" glass.
    return (base == 1 && (t.width() >= 400 || large())) ? 2 : base;
}

// See theme.h. A stored step is -2..+2 around the size each board was built
// with; every consumer clamps it to the sizes it actually has. A stored face
// is 0 (as built), 1 PIXEL (the 6x8 system font) or 2 SMOOTH (font 2).
int8_t fontDelta(Settings::FontKind k) {
    int s = (int)Settings::fontStep(k) - 2;
    return (int8_t)(s < -2 ? -2 : s > 2 ? 2 : s);
}

// How tall a face is: the system font 8 rows a size, font 2 16, font 4 26.
static int faceH(uint8_t font, uint8_t size) { return (font == 4 ? 26 : font == 2 ? 16 : 8) * size; }

// His bubble, a rung at a time. In font 2 (SMOOTH, as built): the 6x8 font,
// font 2, font 2 doubled, font 2 tripled -- the 2.8in boards speak on the
// second rung and the large panels on the third. In PIXEL: the system font
// at 1, 2, 3, 4. BUBBLES moves the rung. speechFontOn() picks it and the
// rest answer for the rung it picked, so a caller that steps down with
// speechFontSmaller() gets measurements that match.
static int  s_speechLvl = 1, s_speechBase = 1;

static bool s_speechPixel = false;

static void speechApply(TFT_eSPI& t) {
    if (s_speechPixel)    { t.setTextFont(1); t.setTextSize((uint8_t)(s_speechLvl + 1)); return; }
    if (s_speechLvl == 0) { t.setTextFont(1); t.setTextSize(1); return; }
    bubbleFontOn(t);
    t.setTextSize(s_speechLvl);
}

void speechFontOn(TFT_eSPI& t, bool small) {
    s_speechBase  = small ? 1 : (s_large ? 2 : 1);
    s_speechPixel = !small && Settings::fontFace(Settings::FontKind::BUBBLES) == 1;
    int lvl = small ? 1 : s_speechBase + fontDelta(Settings::FontKind::BUBBLES);
    s_speechLvl = lvl < 0 ? 0 : lvl > 3 ? 3 : lvl;
    speechApply(t);
}

bool speechAboveNormal()        { return s_speechLvl > s_speechBase; }

void speechFontSmaller(TFT_eSPI& t) { if (s_speechLvl > 0) { s_speechLvl--; speechApply(t); } }

int speechScale()  { return s_speechPixel ? s_speechLvl + 1 : (s_speechLvl == 0 ? 1 : s_speechLvl); }

int speechTextH()  { return (s_speechPixel || s_speechLvl == 0) ? 8 : bubbleTextH(); }

int speechAscent() { return (s_speechPixel || s_speechLvl == 0) ? 0 : bubbleAscent(); }

// A button label's faces, smallest first: the system font, font 2 (as tall
// as size 2, about as narrow as size 1), then the system font at 2, 3, 4.
// BUTTONS moves a label along this from the face it would have had, and the
// label takes the biggest face at or below that which its box can hold. Set
// to PIXEL or SMOOTH, it moves along that face's own sizes instead.
struct LabelFace { uint8_t font, size; };

static const LabelFace LABEL_FACES[]  = { {1, 1}, {2, 1}, {1, 2}, {1, 3}, {1, 4} };

static const LabelFace PIXEL_FACES[]  = { {1, 1}, {1, 2}, {1, 3}, {1, 4} };

static const LabelFace SMOOTH_FACES[] = { {2, 1}, {4, 1}, {2, 2}, {2, 3} };


static const int SMOOTH_N = sizeof(SMOOTH_FACES) / sizeof(SMOOTH_FACES[0]);

// Moves from (font, size) by the BUTTONS step to the biggest face that fits
// maxW x maxH, and leaves it set. Nothing smaller than the ladder's first.
static void labelFaceStep(TFT_eSPI& t, const char* label, uint8_t& font, uint8_t& size, int maxW, int maxH) {
    const int8_t step = fontDelta(Settings::FontKind::BUTTONS);
    const uint8_t face = Settings::fontFace(Settings::FontKind::BUTTONS);
    if (step == 0 && face == 0) return;
    const LabelFace* L = face == 1 ? PIXEL_FACES : face == 2 ? SMOOTH_FACES : LABEL_FACES;
    const int n = face == 1 ? 4 : face == 2 ? SMOOTH_N : 5;
    // Where the face it would have had sits on this ladder: the tallest
    // rung no taller than it (the first, if all are).
    const int h0 = faceH(font, size);
    int at = 0;
    for (int i = 0; i < n; i++) {
        const int hi = faceH(L[i].font, L[i].size);
        if (hi < h0 || (hi == h0 && (face != 0 || (L[i].font == font && L[i].size == size)))) at = i;
    }
    int target = at + step;
    if (target < 0) target = 0;
    if (target >= n) target = n - 1;
    // The biggest that fits; when not even the ladder's first does (a box
    // sized for the 6x8 font, set to SMOOTH), the face it would have had.
    for (int i = target; i >= 0; i--) {
        t.setTextFont(L[i].font);
        t.setTextSize(L[i].size);
        if (t.textWidth(label) <= maxW && t.fontHeight() <= maxH) {
            font = L[i].font; size = L[i].size;
            break;
        }
    }
    t.setTextFont(font);
    t.setTextSize(size);
}

void drawButton(TFT_eSPI& t, int x, int y, int w, int h,
                const char* label, bool pressed, uint8_t textSize) {
    uint16_t fill = pressed ? PURPLE : BG;
    uint16_t fg   = pressed ? labelOn(PURPLE) : CYAN;
    t.fillRect(x, y, w, h, fill);
    t.drawRect(x, y, w, h, PURPLE);
    // Button labels are short and their boxes grew with the panel, so this is
    // the safest place for the step-up. But not every button is a third of the
    // screen: the phone screen's QWERTY and BACK sit in boxes sized for size-1
    // text, and the bigger label ran straight out of them with its brackets
    // cut off. So the step-up has to earn its place -- measure it, and keep
    // the smaller size if it does not fit. That makes this self-limiting for
    // every button on every screen instead of a list of exceptions.
    uint8_t ts = uiTextSize(t, textSize);
    if (ts != textSize) {
        t.setTextSize(ts);
        if (t.textWidth(label) > w - 6 || t.fontHeight() > h - 2) ts = textSize;
    }
    // A large panel's label that cannot take size 2 gets font 2 rather than
    // the 8-row system font: about as wide, half again as tall.
    bool body = false;
    if (ts == 1 && textSize == 1 && large() && h >= 20) {
        t.setTextFont(2);
        t.setTextSize(1);
        body = t.textWidth(label) <= w - 6;
        if (!body) t.setTextFont(1);
    }
    // FONTS > BUTTONS moves that along the label faces, measured against
    // the box like the step-up above.
    uint8_t font = body ? 2 : 1;
    t.setTextSize(ts);
    labelFaceStep(t, label, font, ts, w - 6, h - 2);
    t.setTextColor(fg, fill);
    int tw = t.textWidth(label);
    int th = t.fontHeight();
    t.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    t.print(label);
    if (font != 1) t.setTextFont(1);
}

ButtonBarGeom computeButtonBar(int screenW, int screenH) {
    ButtonBarGeom g;
    // Half of the original 40px (which was sized to comfortably clear
    // ~9mm finger-touch-target guidance) — explicitly requested smaller
    // to free up more room above for content. Still tappable, just a
    // tighter target than the original guidance-driven size.
    g.h = 20;
    const int margin = 8, gap = 8;
    g.y = screenH - g.h - 6;
    int bw = (screenW - 2 * margin - 2 * gap) / 3;
    g.w[0] = g.w[1] = g.w[2] = bw;
    g.x[0] = margin;
    g.x[1] = g.x[0] + bw + gap;
    g.x[2] = g.x[1] + bw + gap;
    return g;
}

// TFT_eSPI has fillTriangle but no polygon fill, and a wing lobe is an
// 18-vertex shape. Fanning it into triangles costs ~16 fillTriangle calls
// per wing, and each of those runs its own scanline pass over a bounding
// box that overlaps its neighbours'. One scanline pass over the whole
// polygon is a single drawFastHLine per row instead -- about 30 row fills
// for a wing, and a row fill is the cheapest thing the sprite can do.
//
// Even-odd rule, so a wing that curls back over itself still fills
// correctly. MAXHIT is generous: the curled variants cross a scanline
// four times at most.
static void fillPoly(TFT_eSPI& t, const int16_t* xs, const int16_t* ys,
                     uint8_t n, uint16_t col) {
    if (n < 3) return;
    int16_t ymin = ys[0], ymax = ys[0];
    for (uint8_t i = 1; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    static const uint8_t MAXHIT = 12;
    for (int16_t y = ymin; y <= ymax; y++) {
        int16_t xh[MAXHIT];
        uint8_t hits = 0;
        for (uint8_t i = 0; i < n && hits < MAXHIT; i++) {
            const uint8_t j = (uint8_t)((i + 1) % n);
            const int16_t y1 = ys[i], y2 = ys[j];
            if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y)) {
                xh[hits++] = (int16_t)(xs[i] +
                    (int32_t)(y - y1) * (xs[j] - xs[i]) / (y2 - y1));
            }
        }
        for (uint8_t a = 1; a < hits; a++) {          // tiny insertion sort
            const int16_t v = xh[a];
            int8_t b = (int8_t)a - 1;
            while (b >= 0 && xh[b] > v) { xh[b + 1] = xh[b]; b--; }
            xh[b + 1] = v;
        }
        for (uint8_t a = 0; (uint8_t)(a + 1) < hits; a += 2)
            t.drawFastHLine(xh[a], y, xh[a + 1] - xh[a] + 1, col);
    }
}

// Wing width along its length, sampled at the 9 points the wing is built
// from. Baked rather than evaluated because the real curve is
// (1-t^5)^0.45 * (0.4 + 0.6*min(1, t/0.3)), and two powf() calls per point
// per wing per sprite per frame is a lot of transcendental for a shape
// that never changes. The profile holds its width through the middle and
// drops only at the very end -- a width that falls off linearly gives a
// spike, and the tip has to read as rounded.
static const float WING_PROFILE[9] = {
    0.400f, 0.650f, 0.900f, 0.997f, 0.986f, 0.956f, 0.885f, 0.724f, 0.0f
};

// One rounded bird wing: a curved lobe with a few feather divisions drawn
// back onto it. It beats by swinging about the shoulder, which is what a
// bird does -- a wing that only slides up and down reads as being dragged.
void drawWing(TFT_eSPI& t, float sx, float sy, float len, float angDeg,
              float flap, float width, uint8_t ndiv,
              uint16_t body, uint16_t edge, float curl, float lift,
              uint16_t shade) {
    static const uint8_t N = 8;
    const float step = len / (float)N;
    const float da   = curl * 60.0f * 0.017453293f / (float)N;
    float a  = (angDeg + flap * lift) * 0.017453293f;
    float px = sx, py = sy;

    int16_t tx[N + 1], ty[N + 1], bx[N + 1], by[N + 1];
    int16_t cx[N + 1], cy[N + 1];
    for (uint8_t i = 0; i <= N; i++) {
        const float wgt = WING_PROFILE[i] * width * len;
        const float nx = -sinf(a), ny = cosf(a);
        cx[i] = (int16_t)px;              cy[i] = (int16_t)py;
        tx[i] = (int16_t)(px + nx * wgt * 0.58f);
        ty[i] = (int16_t)(py + ny * wgt * 0.58f);
        bx[i] = (int16_t)(px - nx * wgt * 0.44f);
        by[i] = (int16_t)(py - ny * wgt * 0.44f);
        px += cosf(a) * step;
        py += sinf(a) * step;
        a  += da;
    }

    int16_t hx[2 * (N + 1)], hy[2 * (N + 1)];
    uint8_t n = 0;
    for (uint8_t i = 0; i <= N; i++)      { hx[n] = tx[i]; hy[n] = ty[i]; n++; }
    for (int8_t i = (int8_t)N; i >= 0; i--) { hx[n] = bx[i]; hy[n] = by[i]; n++; }
    fillPoly(t, hx, hy, n, body);

    for (uint8_t i = 0; i < n; i++) {
        const uint8_t j = (uint8_t)((i + 1) % n);
        t.drawLine(hx[i], hy[i], hx[j], hy[j], edge);
    }
    // Shade whichever edge actually ends up LOWER on screen, rather than
    // always the same array. The flock only ever wears wings on one flank
    // so this never showed there, but Squachy wears a mirrored pair -- and
    // mirroring flips the sign of the normal, which swaps which of top/bot
    // is the lower edge. A fixed choice therefore put the shadow under one
    // wing and over the other, which reads as one wing being upside down.
    const bool shadeTop = ty[N / 2] > by[N / 2];
    const int16_t* sxArr = shadeTop ? tx : bx;
    const int16_t* syArr = shadeTop ? ty : by;

    // 2*(N+1), not N+3. The shade polygon walks the centreline out and the
    // shaded edge back, so it holds two runs of (N-1) points -- 14 at N=8,
    // where N+3 is 11. The three-entry overflow ran off the end of shx into
    // shy, so three coordinates became garbage and fillPoly drew spans to
    // wherever they landed: stray white lines trailing off the wings.
    int16_t shx[2 * (N + 1)], shy[2 * (N + 1)];
    uint8_t sn = 0;
    for (uint8_t i = 2; i <= N; i++) { shx[sn] = cx[i]; shy[sn] = cy[i]; sn++; }
    for (int8_t i = (int8_t)N; i >= 2; i--) { shx[sn] = sxArr[i]; shy[sn] = syArr[i]; sn++; }
    fillPoly(t, shx, shy, sn, shade);

    for (uint8_t d = 1; d <= ndiv; d++) {
        uint8_t i0 = (uint8_t)((float)N * (0.26f + 0.16f * (float)d));
        uint8_t i1 = (uint8_t)(i0 + 2);
        if (i1 > N) i1 = N;
        if (i1 <= i0) break;
        t.drawLine(cx[i0], cy[i0], sxArr[i1], syArr[i1], edge);
    }
}

static const BangersFont::Glyph* bangersFind(char c, BangersSize size) {
    const BangersFont::Glyph* table = (size == BangersSize::LG) ? BangersFont::LG_GLYPHS : BangersFont::MD_GLYPHS;
    uint8_t count = (size == BangersSize::LG) ? BangersFont::LG_GLYPH_COUNT : BangersFont::MD_GLYPH_COUNT;
    for (uint8_t i = 0; i < count; i++) {
        if (table[i].ch == c) return &table[i];
    }
    return nullptr;
}

int bangersTextWidth(const char* s, BangersSize size) {
    int w = 0;
    for (const char* p = s; *p; p++) {
        const BangersFont::Glyph* g = bangersFind(*p, size);
        if (g) w += g->advance;
    }
    return w;
}

// Random brief "signal corruption" glitch on Bangers headline text --
// a whole-text x-jitter plus scattered horizontal scanline dropouts,
// refreshed every ~40ms during a short burst that fires every 5-10s.
// Deterministic from `now` (a cheap multiplicative hash, not repeated
// random() calls) rather than rolling fresh dice per row: several
// callers redraw the same string many times per frame at tiny offsets
// to backfill a solid-color outline behind the real fill color (see
// ui_alert.cpp/ui_watchalert.cpp) -- if each of those passes rolled
// its own random jitter they'd all land differently and the outline
// would smear apart from the fill instead of tearing together like
// one corrupted signal.
static uint32_t s_glitchNextAt   = 0;

static uint32_t s_glitchUntil    = 0;

static uint8_t  s_glitchLevel    = 1;   // 0..4, see triggerGlitchBurst()'s comment

// Per-level tuning -- index is the clamped 0..4 intensity. Deliberately
// NOT a smooth curve: level 3 is where a full-screen tear (see
// drawGlitchStatic()) joins in on top of everything else, so levels 3
// and 4 jump harder than the 0->1->2 ramp does.
static const uint32_t BURST_MS_BY_LEVEL[]    = { 150, 200, 260, 320, 420 };

static const int      JITTER_MAX_BY_LEVEL[]  = {   1,   2,   3,   4,   5 };

static const int      DROPOUT_MOD_BY_LEVEL[] = {  10,   6,   4,   3,   2 };  // 1-in-N rows drop

static uint32_t glitchHash(uint32_t x) {
    x *= 2654435761u;
    x ^= x >> 15;
    return x;
}

// Advances the shared burst timer and reports whether `now` falls
// inside one. Safe to call more than once for the same `now` (e.g.
// once from drawGlitchStatic() and again from several drawBangersText()
// calls within one frame) -- once the first call arms a burst,
// `now < s_glitchUntil` makes every later call in that same instant
// see it's already armed instead of re-rolling.
static bool updateGlitchState(uint32_t now) {
    if (s_glitchNextAt == 0) s_glitchNextAt = now + (uint32_t)random(5000, 10001);
    if (now >= s_glitchNextAt && now >= s_glitchUntil) {
        // Ambient, nobody-asked-for-it bursts always stay mild (level
        // 1) so idle screens read as consistent flavor, not a ramping
        // spectacle -- only an explicit triggerGlitchBurst() call asks
        // for something louder.
        s_glitchLevel  = 1;
        s_glitchUntil  = now + BURST_MS_BY_LEVEL[s_glitchLevel];
        s_glitchNextAt = s_glitchUntil + (uint32_t)random(5000, 10001);
    }
    return now < s_glitchUntil;
}

// One actual render pass -- shared by the real draw and the ghost copy
// below so both respect the exact same per-row dropout decisions
// (same bucket/row inputs) and tear together instead of independently.
static void drawBangersPass(TFT_eSPI& t, const char* s, int x, int y, uint16_t color,
                             BangersSize size, bool glitching, uint32_t bucket, uint8_t level) {
    int cursorX = x;
    int dropoutMod = DROPOUT_MOD_BY_LEVEL[level];
    for (const char* p = s; *p; p++) {
        const BangersFont::Glyph* g = bangersFind(*p, size);
        if (!g) continue;
        if (g->bitmap) {
            int rowBytes = (g->w + 7) / 8;
            for (int row = 0; row < g->h; row++) {
                // Same dropout decision for a given (bucket, row) no
                // matter which glyph or which pass is drawing it, so a
                // dropped scanline tears across the whole word at once
                // instead of a random per-letter speckle.
                if (glitching && (glitchHash(bucket * 131u + row) % dropoutMod) == 0) continue;
                const uint8_t* rowPtr = g->bitmap + row * rowBytes;
                int runStart = -1;
                for (int col = 0; col <= g->w; col++) {
                    bool bit = false;
                    if (col < g->w) {
                        uint8_t byte = rowPtr[col / 8];
                        bit = (byte >> (7 - (col % 8))) & 1;
                    }
                    if (bit && runStart < 0) runStart = col;
                    if (!bit && runStart >= 0) {
                        t.drawFastHLine(cursorX + g->xoff + runStart, y + g->yoff + row, col - runStart, color);
                        runStart = -1;
                    }
                }
            }
        }
        cursorX += g->advance;
    }
}

void drawBangersText(TFT_eSPI& t, int x, int y, const char* s, uint16_t color, BangersSize size) {
    uint32_t now = millis();
    bool glitching = updateGlitchState(now);
    uint8_t level = s_glitchLevel;
    uint32_t bucket = now / 40;
    int jitterMax = JITTER_MAX_BY_LEVEL[level];
    int jitterX = glitching ? (int)(glitchHash(bucket) % (2 * jitterMax + 1)) - jitterMax : 0;

    // Chromatic-split ghost -- a faint offset copy in a contrasting
    // color, drawn first so the real pass paints over/beside it.
    // Skipped for BLACK: that's the color the outline trick
    // (ui_clear.cpp/ui_watchalert.cpp) uses for its 24-pass solid
    // backing behind the one real colored pass that follows -- ghosting
    // each of those 24 would be wasted work and visual mud, not fringe.
    // Offset grows with level too, so the fringe visibly widens along
    // with everything else instead of staying a fixed 2px at any
    // intensity.
    if (glitching && color != BLACK) {
        uint16_t ghostColor = blend(CYAN, VAPOR_PINK, (uint16_t)(glitchHash(bucket + 7) % 256));
        int ghostOfs = 2 + level;
        drawBangersPass(t, s, x + jitterX + ghostOfs, y - 1, ghostColor, size, glitching, bucket, level);
    }

    drawBangersPass(t, s, x + jitterX, y, color, size, glitching, bucket, level);
}

// The outline behind a headline, in one pass instead of twenty-four.
//
// The 24-pass trick draws the same text at every offset in a 5x5 square but
// the centre, each pass scanning every glyph bitmap bit by bit and drawing
// every ink run again. NEARBY at LG that way was 14.8 ms of every frame on
// the main screen -- measured -- which is as much as the whole animated
// background and nearly as much as pushing the frame to the panel.
//
// Same picture, one scan: every ink run becomes one block, the run widened
// by `radius` each side and `radius` rows above and below. The union of
// those blocks IS the union of the 24 offsets (the one pixel the offsets
// miss, the run's own, the colour pass then paints over in both versions),
// so the result is identical to the pixel -- checked against the emulator's
// renders, not asserted.
//
// The glitch is reproduced, not skipped: same jitter, same dropped rows,
// from the same hash of the same bucket the colour pass will use, so a
// burst tears the outline and the fill together the way it did before.
void drawBangersOutline(TFT_eSPI& t, int x, int y, const char* s, uint16_t color,
                        BangersSize size, uint8_t radius) {
    uint32_t now = millis();
    bool glitching = updateGlitchState(now);
    uint8_t level = s_glitchLevel;
    uint32_t bucket = now / 40;
    int jitterMax = JITTER_MAX_BY_LEVEL[level];
    int jitterX = glitching ? (int)(glitchHash(bucket) % (2 * jitterMax + 1)) - jitterMax : 0;
    int dropoutMod = DROPOUT_MOD_BY_LEVEL[level];
    const int r = radius;
    const int tall = 2 * r + 1;

    int cursorX = x + jitterX;
    for (const char* p = s; *p; p++) {
        const BangersFont::Glyph* g = bangersFind(*p, size);
        if (!g) continue;
        if (g->bitmap) {
            int rowBytes = (g->w + 7) / 8;
            for (int row = 0; row < g->h; row++) {
                if (glitching && (glitchHash(bucket * 131u + row) % dropoutMod) == 0) continue;
                const uint8_t* rowPtr = g->bitmap + row * rowBytes;
                int runStart = -1;
                for (int col = 0; col <= g->w; col++) {
                    bool bit = false;
                    if (col < g->w) {
                        uint8_t byte = rowPtr[col / 8];
                        bit = (byte >> (7 - (col % 8))) & 1;
                    }
                    if (bit && runStart < 0) runStart = col;
                    if (!bit && runStart >= 0) {
                        t.fillRect(cursorX + g->xoff + runStart - r, y + g->yoff + row - r,
                                   (col - runStart) + 2 * r, tall, color);
                        runStart = -1;
                    }
                }
            }
        }
        cursorX += g->advance;
    }
}

// Ported from squachy.cpp verbatim (was a private static there,
// duplicated for LOG's MORE INFO panel until this promotion) -- see
// its declaration in theme.h for the full behavior notes.
//
// lines[][48], not [40]: confirmed on real hardware that a wide-enough
// maxW (a caller with real screen width to spare) let a line accumulate
// several words -- each individually well under maxW in pixels -- past
// the buffer's own char capacity before the width check ever tripped,
// silently truncating mid-word ("...Amazon's vid" instead of "video").
// The strlen() check added below is the actual fix (forces a break the
// moment the buffer itself would fill, independent of maxW); the wider
// buffer just means that happens less often in the first place.
// One body for both row widths: 48 for everything that has always used it,
// 80 for running text on the large panels, where font 2 fits 70 letters to a
// landscape line and a 48-letter row stopped every line two thirds across.
template <size_t N>
static uint8_t wrapInto(TFT_eSPI& t, const char* text, int maxW,
                        char lines[][N], uint8_t maxLines) {
    // 320, not the original 160 -- fine for every short quip/bubble
    // this ran on originally, but LOG's MORE INFO panel passes real
    // paragraph-length explanations (the RSSI/confidence primer alone
    // is ~290 chars), which strncpy was silently truncating before a
    // single word ever got wrapped. The 80-letter form (the large panels)
    // takes 640: the longest MORE INFO explanations run past 320, and with
    // paging there is room to show every word of them.
    char buf[N == 80 ? 640 : 320];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    uint8_t n = 0;
    char lineBuf[N] = "";
    char* word = strtok(buf, " ");
    while (word) {
        char trial[N];
        if (lineBuf[0]) snprintf(trial, sizeof(trial), "%s %s", lineBuf, word);
        else            snprintf(trial, sizeof(trial), "%s", word);
        bool tooWide = lineBuf[0] &&
                       (t.textWidth(trial) > maxW || strlen(trial) >= sizeof(lineBuf) - 1);
        if (tooWide) {
            if (n >= maxLines - 1) break; // out of lines -- let the rest go rather than drop it silently
            strncpy(lines[n], lineBuf, sizeof(lines[n]) - 1); lines[n][sizeof(lines[n]) - 1] = 0; n++;
            strncpy(lineBuf, word, sizeof(lineBuf) - 1); lineBuf[sizeof(lineBuf) - 1] = 0;
        } else {
            strncpy(lineBuf, trial, sizeof(lineBuf) - 1); lineBuf[sizeof(lineBuf) - 1] = 0;
        }
        word = strtok(nullptr, " ");
    }
    if (lineBuf[0] && n < maxLines) {
        strncpy(lines[n], lineBuf, sizeof(lines[n]) - 1); lines[n][sizeof(lines[n]) - 1] = 0; n++;
    }
    return n;
}

uint8_t wrapText(TFT_eSPI& t, const char* text, int maxW,
                 char lines[][48], uint8_t maxLines) {
    return wrapInto<48>(t, text, maxW, lines, maxLines);
}

uint8_t wrapText(TFT_eSPI& t, const char* text, int maxW,
                 char lines[][80], uint8_t maxLines) {
    return wrapInto<80>(t, text, maxW, lines, maxLines);
}


// What Squachy's drawing code asks of the bigger app's screens; nothing here.
uint16_t colorFor(DetectionType) { return GREEN; }
void showToast(const char*, const char*, uint16_t, uint32_t) {}
void setRedGlyph(bool) {}

}  // namespace Theme
