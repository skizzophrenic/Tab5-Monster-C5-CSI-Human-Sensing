// Host test for src/sense_c5/csi_vitals: synthetic CSI with a breathing
// wobble on some tones, a heartbeat far smaller, noise, gain steps and uneven
// arrival times. c1 must follow the breathing; the gain steps must not show.
//   g++ -O2 -DSENSE_MODEL_HOST -I../../src/sense_c5 test_vitals.cpp ../../src/sense_c5/csi_vitals.cpp -o t && ./t
#include "csi_vitals.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double urand() { return rand() / (RAND_MAX + 1.0); }
static double gauss() { return sqrt(-2 * log(urand() + 1e-12)) * cos(6.283185307 * urand()); }

int main() {
  srand(42);
  CsiVitals v;
  if (!v.begin()) { printf("FAIL alloc\n"); return 1; }
  const int T = 26;
  double base[T], wBreath[T], wHeart[T], phase[T];
  for (int i = 0; i < T; i++) { base[i] = 200 + 300 * urand(); wBreath[i] = (urand() - 0.5) * 0.06; wHeart[i] = (urand() - 0.5) * 0.004; phase[i] = urand() * 6.28; }
  const double fb = 14.0 / 60, fh = 66.0 / 60;
  double t = 0; uint32_t ms = 0;
  int frames = 0; double sxy = 0, sxx = 0, syy = 0; int nc = 0; int lastQ = 0;
  // a gain step every 7 s: every tone x1.3, the way an AGC change looks
  while (t < 90) {
    t += 0.012 + 0.025 * urand();   // ~50 a second, uneven
    ms = (uint32_t)(t * 1000);
    const double gain = (int)(t / 7) % 2 ? 1.3 : 1.0;
    const double breath = sin(6.283185307 * fb * t), heart = sin(6.283185307 * fh * t);
    int8_t bytes[2 + T * 4 + 4];
    memset(bytes, 0, sizeof bytes);
    for (int i = 0; i < T; i++) {
      const double amp = gain * base[i] * (1 + wBreath[i] * breath + wHeart[i] * heart + 0.004 * gauss());
      const int re = (int)lround(amp * cos(phase[i])), im = (int)lround(amp * sin(phase[i]));
      const uint16_t wr = (uint16_t)(re & 0x0FFF), wi = (uint16_t)(im & 0x0FFF);
      bytes[i * 4 + 0] = (int8_t)(wr & 0xFF); bytes[i * 4 + 1] = (int8_t)(wr >> 8);
      bytes[i * 4 + 2] = (int8_t)(wi & 0xFF); bytes[i * 4 + 3] = (int8_t)(wi >> 8);
    }
    v.addSample(bytes, T * 4, false);
    float c1, c2; uint8_t n, q;
    if (v.tick(ms, c1, c2, n, q)) {
      frames++; lastQ = q;
      if (t > 30) { const double ft = frames * 0.05; (void)ft; const double b = sin(6.283185307 * fb * (ms / 1000.0)); sxy += c1 * b; sxx += (double)c1 * c1; syy += b * b; nc++; }
    }
  }
  const double corr = sxy / sqrt(sxx * syy + 1e-30);
  printf("tones %d  frames %d (%.1f/s)  q %d  |corr(c1, breathing)| %.3f over %d frames\n", v.tones(), frames, frames / 90.0, lastQ, fabs(corr), nc);
  const bool ok = v.tones() == T && frames > 90 * 18 && fabs(corr) > 0.85;
  printf(ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}
