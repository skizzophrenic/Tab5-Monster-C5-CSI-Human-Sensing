// Tab5 Monster C5 CSI Human Sensing -- the sensing radio: an ESP32-C5 that watches the WiFi
// channel's shape and scores motion in it.
//
// Runs on the M5MonsterC5 (C5Lab's ESP32-C5-WROOM-1U add-on for the Tab5),
// in place of its stock JanOS. It owns no screen and no buttons. It joins a
// network or parks on a channel, asks the radio for channel state information
// (CSI: how each of 64 sub-carriers arrived, amplitude and phase, for every
// frame heard), runs one motion model per transmitter it watches, fuses
// them, and says what it sees ten times a second as one ASCII line on its
// UART0 (the Tab5's M5-Bus, P4 pins 37/38) and on its own USB-C. The line
// format and the commands are in include/sense_proto.h.
//
// What it is and is not: it tells a still room from a room with something
// moving in it, on the radio path between the transmitter and this chip.
// It does not know who or what moved, how many, or where; a fan, a pet or a
// curtain in a draught move the channel too. A calibration with the room
// still is what everything is measured against.
//
// The model is ESP32Marauder's (MIT, Just Call Me Koko), see
// csi_motion_model.h. The second-transmitter idea (an ESP32 beaconing at a
// fixed rate as the reference) is Evil-M5Project's CSI Radar (7h30th3r0n3);
// here any transmitter busy enough is modelled, so a beacon, the home router
// or whatever is loud on the channel all work, with no pairing step.
#if defined(SENSE_C5)
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <soc/lp_aon_reg.h>
#include <string.h>
#include "sense_proto.h"
#include "csi_motion_model.h"
#include "csi_vitals.h"
#include "ping/ping_sock.h"
#include "lwip/ip_addr.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// ---- Tunables --------------------------------------------------------------
static const uint8_t  kQueueSize   = 64;     // CSI samples waiting for the loop
static const uint16_t kMaxCsiBytes = 256;    // 64 tones x 2 x 16 bit
static const uint8_t  kMaxTargets  = 4;      // transmitters modelled at once
static const uint8_t  kSourceSlots = 16;     // transmitters counted for ranking
static const uint32_t kRankMs      = 2000;   // how often targets are re-picked
static const uint32_t kEvictMs     = 10000;  // a silent target is dropped after this
static const uint16_t kMinRatePerS = 2;      // a source slower than this is never a target
static const uint32_t kPingMs      = 50;     // STA mode: one datagram to the gateway per this
static const uint32_t kInfoMs      = 60000;

// ---- CSI sample queue (filled in the WiFi task, drained in loop) ----------
struct Sample {
  uint16_t length;
  int8_t   rssi;
  bool     first_word_invalid;
  uint8_t  mac[6];
  uint8_t  channel;
  uint32_t at;
  int8_t   iq[kMaxCsiBytes];
};
static Sample            s_queue[kQueueSize];
static volatile uint8_t  s_wr = 0, s_rd = 0;
static volatile uint32_t s_accepted = 0, s_dropped = 0, s_seen = 0;

// Who is transmitting, counted in the callback so ranking costs nothing there.
struct Source { uint8_t mac[6]; volatile uint16_t frames; uint32_t lastAt; };
static Source s_sources[kSourceSlots];

// ---- Targets ---------------------------------------------------------------
struct Target {
  bool     used;
  uint8_t  mac[6];
  uint32_t accepted, dropped, lastAt;
  CsiMotionModel model;
};
static Target  s_targets[kMaxTargets];
static bool    s_lockTarget = false;   // STA mode: only the AP
static uint8_t s_lockMac[6];

// ---- State -----------------------------------------------------------------
static Preferences s_prefs;
static uint8_t  s_mode = SENSE_MODE_OFF;
static uint16_t s_chan = 1;
static uint32_t s_windowMs = 1000, s_calMs = 30000;
// PRESENCE: on with the first MOTION, off once there has been no MOTION for
// s_holdMs. Measured on 5 GHz (2026-11-07): an empty room never raised it,
// a person walking held it 100 %, and a person sitting at the desk raised it
// 24 / 48 / 69 / 83 % of the time with a 5 / 10 / 15 / 20 s hold. 15 s is
// the default; `hold <ms>` changes it.
static uint32_t s_holdMs = 15000;
static uint32_t s_lastMotionMs = 0;
static bool     s_presence = false;
static char     s_ssid[33], s_pass[65];
static uint8_t  s_raw = 0;          // 1: a line per sample; 2: also the bytes past info->len
static bool     s_csiOn = false;
static bool     s_wifiUp = false;   // WiFi.mode() has run once: disconnect() before it only logs an error
static uint32_t s_startedMs = 0, s_lastLineMs = 0, s_lastRankMs = 0, s_lastInfoMs = 0, s_lastPingMs = 0, s_lastFusionMs = 0;
static uint32_t s_joinStartedMs = 0;
static WiFiUDP  s_udp;

// VITALS (the screen's breathing monitor): "vitals on <ssid>\t<pass>" joins
// that network just for the session (nothing saved), pings the router 50
// times a second so its replies are a steady stream of frames from one place,
// and sends the chest signal ($V) 20 times a second. "vitals off" puts the
// radio back the way it was. Without a network it stays parked and follows
// the busiest transmitter instead.
static const uint32_t kVitalsPingMs = 20;
static bool     s_vitals = false;
static CsiVitals s_vit;
static uint8_t  s_vitPrevMode = SENSE_MODE_PARK;
static uint16_t s_vitPrevChan = 1;
static int      s_vitTarget = -1;
static uint8_t  s_vitMac[6];
static esp_ping_handle_t s_ping = nullptr;
static uint32_t s_vitFrames = 0;
// The network VITALS joins when the screen doesn't lend one (the Tab5 has no
// WiFi of its own yet): set once with "vitalsnet <ssid>TAB<pass>", kept in
// NVS apart from the radio's own join settings, used only during VITALS.
static char     s_vssid[33], s_vpass[65];
// Why the last join attempt dropped, from the WiFi driver (wifi_err_reason_t).
static volatile uint16_t s_discReason = 0;
static volatile uint16_t s_lastEvent = 0;
static const char* reasonText(uint16_t r) {
  switch (r) {
    case 2: return "auth expired";
    case 15: return "4-way handshake timed out: wrong password?";
    case 201: return "no access point with that name in range";
    case 202: return "authentication failed: wrong password?";
    case 203: return "association refused";
    case 204: return "handshake timed out: wrong password?";
    case 205: return "connection failed";
    case 210: return "found it, but its security type isn't one this radio uses";
    case 211: return "found it, but its security is below the radio's minimum";
    default: return "";
  }
}

// Fused output.
static uint8_t  s_score = 0, s_amp = 0, s_phase = 0, s_corr = 0, s_rel = 0, s_state = SENSE_STATE_CAL;
static uint16_t s_rate = 0, s_live = 0;
static int8_t   s_rssi = -127;
static uint8_t  s_motionVotes = 0, s_stillVotes = 0;

// ---- Output to both ports --------------------------------------------------
static void out(const char* s) { Serial.print(s); Serial0.print(s); }
static void outf(const char* fmt, ...) {
  char b[SENSE_LINE_MAX + 64];
  va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  out(b);
}
static void note(const char* fmt, ...) {
  char b[200];
  va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  outf("# %s\n", b);
}

// ---- The CSI callback: decide fast, copy, go ------------------------------
static void IRAM_ATTR csiCb(void* ctx, wifi_csi_info_t* info) {
  (void)ctx;
  if (!info || !info->buf || info->len < 16) return;
  s_seen++;
  // Count the source, for ranking.
  const uint32_t now = millis();
  int freeSlot = -1, oldest = 0;
  bool counted = false;
  for (int i = 0; i < kSourceSlots; i++) {
    if (s_sources[i].lastAt && memcmp(s_sources[i].mac, info->mac, 6) == 0) {
      if (s_sources[i].frames < 0xFFFF) s_sources[i].frames++;
      s_sources[i].lastAt = now; counted = true; break;
    }
    if (!s_sources[i].lastAt) { if (freeSlot < 0) freeSlot = i; }
    else if (s_sources[i].lastAt < s_sources[oldest].lastAt) oldest = i;
  }
  if (!counted) {
    const int slot = freeSlot >= 0 ? freeSlot : oldest;
    memcpy(s_sources[slot].mac, info->mac, 6);
    s_sources[slot].frames = 1; s_sources[slot].lastAt = now;
  }
  // Only samples from a target are queued (the rest were just counted).
  if (s_lockTarget) {
    if (memcmp(info->mac, s_lockMac, 6) != 0) return;
  } else {
    bool want = false;
    for (int i = 0; i < kMaxTargets; i++)
      if (s_targets[i].used && memcmp(s_targets[i].mac, info->mac, 6) == 0) { want = true; break; }
    if (!want) return;
  }
  const uint8_t next = (uint8_t)((s_wr + 1) % kQueueSize);
  if (next == s_rd) { s_dropped++; return; }
  Sample& s = s_queue[s_wr];
  // raw 2 (bench only): also copy as far again past info->len, to see
  // whether the length is counted in bytes or in 16-bit words on this chip.
  const uint16_t want = s_raw == 2 ? (uint16_t)(info->len * 2) : info->len;
  s.length = want > kMaxCsiBytes ? kMaxCsiBytes : want;
  s.rssi = info->rx_ctrl.rssi;
  s.first_word_invalid = info->first_word_invalid;
  s.channel = info->rx_ctrl.channel;
  s.at = now;
  memcpy(s.mac, info->mac, 6);
  memcpy(s.iq, info->buf, s.length);
  s_wr = next;
  s_accepted++;
}

// ---- Radio ----------------------------------------------------------------
static bool csiStart() {
  wifi_csi_config_t config = {};
  // ESP32Marauder's C5 recipe: force the L-LTF shape for every modulation,
  // so one transmitter's samples always have the same 64 tones, whatever
  // rate it sent at. 12-bit I/Q (lltf_bit_mode 0), no ACK dumps.
  config.enable = 1;
  config.acquire_csi_legacy = 1;
  config.acquire_csi_force_lltf = 1;
  config.acquire_csi_ht20 = 1;
  config.acquire_csi_ht40 = 1;
  config.acquire_csi_vht = 1;
  config.acquire_csi_su = 1;
  config.acquire_csi_mu = 1;
  config.acquire_csi_dcm = 1;
  config.acquire_csi_beamformed = 1;
  config.acquire_csi_he_stbc_mode = ESP_CSI_ACQUIRE_STBC_HELTF1;
  config.val_scale_cfg = 0;
  config.dump_ack_en = 0;
  esp_err_t e;
  if ((e = esp_wifi_set_csi_rx_cb(csiCb, nullptr)) != ESP_OK) { note("csi cb: %s", esp_err_to_name(e)); return false; }
  if ((e = esp_wifi_set_csi_config(&config)) != ESP_OK) { note("csi config: %s", esp_err_to_name(e)); return false; }
  if ((e = esp_wifi_set_csi(true)) != ESP_OK) { note("csi on: %s", esp_err_to_name(e)); return false; }
  s_csiOn = true;
  return true;
}

static void csiStop() {
  if (!s_csiOn) return;
  esp_wifi_set_csi(false);
  esp_wifi_set_csi_rx_cb(nullptr, nullptr);
  s_csiOn = false;
}

static void clearTargets() {
  for (int i = 0; i < kMaxTargets; i++) s_targets[i].used = false;
  memset(s_sources, 0, sizeof s_sources);
  s_wr = s_rd = 0; s_accepted = s_dropped = s_seen = 0;
  s_score = s_amp = s_phase = s_corr = s_rel = 0; s_rate = s_live = 0; s_rssi = -127;
  s_motionVotes = s_stillVotes = 0; s_state = SENSE_STATE_CAL;
  s_startedMs = millis(); s_lastFusionMs = s_startedMs;
}

static void radioCommon() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  s_wifiUp = true;
  esp_wifi_set_ps(WIFI_PS_NONE);
  const esp_err_t be = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
  if (be != ESP_OK) note("band mode auto: %s (5 GHz may be off)", esp_err_to_name(be));
}

static void stopPing() {
  if (!s_ping) return;
  esp_ping_stop(s_ping);
  esp_ping_delete_session(s_ping);
  s_ping = nullptr;
}

static void startPing() {
  stopPing();
  const IPAddress gw = WiFi.gatewayIP();
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.count = ESP_PING_COUNT_INFINITE;
  cfg.interval_ms = kVitalsPingMs;
  cfg.timeout_ms = 1000;
  cfg.data_size = 1;
  cfg.task_stack_size = 3072;
  IP_ADDR4(&cfg.target_addr, gw[0], gw[1], gw[2], gw[3]);
  esp_ping_callbacks_t cbs = {};
  if (esp_ping_new_session(&cfg, &cbs, &s_ping) == ESP_OK && esp_ping_start(s_ping) == ESP_OK)
    note("vitals: pinging the router %s every %lu ms", gw.toString().c_str(), (unsigned long)kVitalsPingMs);
  else { note("vitals: ping failed to start"); s_ping = nullptr; }
}

static bool startPark(uint16_t chan) {
  stopPing();
  csiStop();
  if (s_wifiUp) WiFi.disconnect(true, false);
  clearTargets();
  s_lockTarget = false;
  radioCommon();
  esp_err_t e = esp_wifi_set_channel((uint8_t)chan, WIFI_SECOND_CHAN_NONE);
  if (e != ESP_OK) { note("channel %u refused: %s", chan, esp_err_to_name(e)); return false; }
  s_chan = chan;
  wifi_promiscuous_filter_t f = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
  esp_wifi_set_promiscuous_filter(&f);
  if ((e = esp_wifi_set_promiscuous(true)) != ESP_OK) { note("promiscuous: %s", esp_err_to_name(e)); return false; }
  if (!csiStart()) return false;
  s_mode = SENSE_MODE_PARK;
  note("park: channel %u (%s GHz), modelling the %u busiest transmitters", chan, chan > 14 ? "5" : "2.4", kMaxTargets);
  return true;
}

static bool startJoin() {
  if (!s_ssid[0]) { note("join: no network saved"); return false; }
  stopPing();
  csiStop();
  if (s_wifiUp) { esp_wifi_set_promiscuous(false); WiFi.disconnect(true, false); }
  clearTargets();
  s_lockTarget = false;
  radioCommon();
  s_discReason = 0; s_lastEvent = 0;
  const wl_status_t b = WiFi.begin(s_ssid, s_pass);
  s_mode = SENSE_MODE_JOINING;
  s_joinStartedMs = millis();
  note("joining \"%s\"... (begin %d, mode %d)", s_ssid, (int)b, (int)WiFi.getMode());
  return true;
}

// Called once the STA is up: lock onto the AP, and listen to everything on
// its channel (promiscuous) so its beacons count as samples too.
static void onJoined() {
  const uint8_t* b = WiFi.BSSID();
  if (!b) return;
  memcpy(s_lockMac, b, 6);
  s_lockTarget = true;
  s_chan = (uint16_t)WiFi.channel();
  clearTargets();
  Target& t = s_targets[0];
  t.used = true; memcpy(t.mac, s_lockMac, 6); t.accepted = t.dropped = 0; t.lastAt = millis();
  t.model.reset(millis(), s_calMs, s_windowMs);
  wifi_promiscuous_filter_t f = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
  esp_wifi_set_promiscuous_filter(&f);
  esp_wifi_set_promiscuous(true);
  s_udp.begin(55555);
  if (csiStart()) {
    s_mode = SENSE_MODE_STA;
    if (s_vitals) startPing();
    note("joined: BSSID %02X:%02X:%02X:%02X:%02X:%02X channel %u (%s GHz) rssi %d, pinging gateway %s every %lu ms",
         b[0], b[1], b[2], b[3], b[4], b[5], s_chan, s_chan > 14 ? "5" : "2.4", WiFi.RSSI(),
         WiFi.gatewayIP().toString().c_str(), (unsigned long)kPingMs);
  }
}

// ---- Targets: pick the busiest transmitters -------------------------------
static int findTarget(const uint8_t* mac) {
  for (int i = 0; i < kMaxTargets; i++)
    if (s_targets[i].used && memcmp(s_targets[i].mac, mac, 6) == 0) return i;
  return -1;
}

static void rankSources(uint32_t now) {
  if (s_lockTarget) return;
  // Evict silent targets.
  for (int i = 0; i < kMaxTargets; i++)
    if (s_targets[i].used && now - s_targets[i].lastAt > kEvictMs) {
      note("target %02X:%02X:%02X:%02X:%02X:%02X went quiet, dropped", s_targets[i].mac[0], s_targets[i].mac[1],
           s_targets[i].mac[2], s_targets[i].mac[3], s_targets[i].mac[4], s_targets[i].mac[5]);
      s_targets[i].used = false;
    }
  // Snapshot and zero the counters (frames since the last ranking).
  struct Cand { uint8_t mac[6]; uint16_t frames; } cand[kSourceSlots];
  int n = 0;
  for (int i = 0; i < kSourceSlots; i++) {
    const uint16_t f = s_sources[i].frames;
    s_sources[i].frames = 0;
    if (!s_sources[i].lastAt || f == 0) continue;
    if (now - s_sources[i].lastAt > kEvictMs) { s_sources[i].lastAt = 0; continue; }
    memcpy(cand[n].mac, s_sources[i].mac, 6); cand[n].frames = f; n++;
  }
  // Busiest first.
  for (int i = 1; i < n; i++) { Cand c = cand[i]; int j = i; while (j && cand[j - 1].frames < c.frames) { cand[j] = cand[j - 1]; j--; } cand[j] = c; }
  // Fill free slots with the busiest sources not yet modelled.
  const uint16_t minFrames = (uint16_t)(kMinRatePerS * kRankMs / 1000);
  for (int i = 0; i < n; i++) {
    if (cand[i].frames < minFrames) break;
    if (findTarget(cand[i].mac) >= 0) continue;
    int slot = -1;
    for (int k = 0; k < kMaxTargets; k++) if (!s_targets[k].used) { slot = k; break; }
    if (slot < 0) break;
    Target& t = s_targets[slot];
    t.used = true; memcpy(t.mac, cand[i].mac, 6); t.accepted = t.dropped = 0; t.lastAt = now;
    t.model.reset(now, s_calMs, s_windowMs);
    note("target %02X:%02X:%02X:%02X:%02X:%02X: %u frames in %lu ms, calibrating %lu s", t.mac[0], t.mac[1], t.mac[2],
         t.mac[3], t.mac[4], t.mac[5], cand[i].frames, (unsigned long)kRankMs, (unsigned long)(s_calMs / 1000));
  }
}

// ---- Fusion: ESP32Marauder's, over the calibrated, trusted targets --------
static void fuse(uint32_t now) {
  struct Cand { uint8_t score, amp, phase, corr, rel; int8_t rssi; float live; };
  Cand best[3] = {};
  uint32_t relSum = 0, rateSum = 0;
  uint8_t active = 0, calibrating = 0, calPct = 0;
  float liveMax = 0.0f;
  for (int i = 0; i < kMaxTargets; i++) {
    if (!s_targets[i].used) continue;
    CsiMotionModel& m = s_targets[i].model;
    m.tick(now, s_targets[i].accepted, s_targets[i].dropped);
    rateSum += m.samplesPerSecond();
    if (!m.calibrated()) { calibrating++; if (m.calibrationPercent(now) > calPct) calPct = m.calibrationPercent(now); continue; }
    if (m.reliability() < 25) continue;
    active++;
    relSum += m.reliability();
    if (m.live() > liveMax) liveMax = m.live();
    Cand c = { m.motionScore(), m.amplitudeScore(), m.phaseScore(), m.correlationScore(), m.reliability(), m.rssi(), m.live() };
    for (int s = 0; s < 3; s++) {
      if (c.score > best[s].score || (c.score == best[s].score && best[s].rel == 0)) {
        for (int mv = 2; mv > s; mv--) best[mv] = best[mv - 1];
        best[s] = c; break;
      }
    }
  }
  s_rate = (uint16_t)(rateSum > 65535 ? 65535 : rateSum);
  s_rel = active ? (uint8_t)(relSum / active) : 0;
  uint32_t w = 0, ms = 0, as = 0, ps = 0, cs = 0;
  for (int i = 0; i < 3 && best[i].rel; i++) {
    w += best[i].rel; ms += best[i].score * best[i].rel; as += best[i].amp * best[i].rel;
    ps += best[i].phase * best[i].rel; cs += best[i].corr * best[i].rel;
  }
  s_score = w ? (uint8_t)(ms / w) : 0;
  s_amp = w ? (uint8_t)(as / w) : 0;
  s_phase = w ? (uint8_t)(ps / w) : 0;
  s_corr = w ? (uint8_t)(cs / w) : 0;
  s_rssi = best[0].rel ? best[0].rssi : -127;
  if (s_rssi == -127) {
    // Nothing trusted yet (calibrating, or too little data): still say how
    // loud the best target is, so a weak reference shows up straight away.
    for (int i = 0; i < kMaxTargets; i++)
      if (s_targets[i].used && s_targets[i].model.rssi() > s_rssi) s_rssi = s_targets[i].model.rssi();
  }
  float lv = liveMax * 100.0f; if (lv > 1000.0f) lv = 1000.0f;
  s_live = (uint16_t)lv;
  if (!active && calibrating) {
    s_state = SENSE_STATE_CAL; s_rel = calPct / 4;
  } else if (!active) {
    s_state = SENSE_STATE_LOWDATA;
  } else if (s_score >= CsiMotionModel::kMotionThreshold) {
    s_motionVotes = s_motionVotes < 5 ? s_motionVotes + 1 : 5; s_stillVotes = 0;
    s_state = s_motionVotes >= 2 ? SENSE_STATE_MOTION : SENSE_STATE_POSSIBLE;
  } else if (s_score >= CsiMotionModel::kPossibleMotionThreshold) {
    s_motionVotes = s_stillVotes = 0; s_state = SENSE_STATE_POSSIBLE;
  } else {
    s_motionVotes = 0; s_stillVotes = s_stillVotes < 3 ? s_stillVotes + 1 : 3;
    if (s_stillVotes >= 2) s_state = SENSE_STATE_STILL;
  }
}

static uint8_t calPercent(uint32_t now) {
  uint8_t pct = 100; bool any = false;
  for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used) {
    any = true;
    const uint8_t p = s_targets[i].model.calibrationPercent(now);
    if (p < pct) pct = p;
  }
  return any ? pct : 0;
}

static uint8_t targetCount() { uint8_t n = 0; for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used) n++; return n; }

// ---- Lines out ------------------------------------------------------------
static void sendStatus(uint32_t now) {
  outf("$S,%lu,%u,%u,%u,%u,%u,%d,%u,%u,%u,%u,%u,%u,%u,%u,%lu,%u\n",
       (unsigned long)now, s_score, s_state, s_rate, s_chan, s_chan > 14 ? 5 : 2, (int)s_rssi, s_rel, calPercent(now),
       targetCount(), s_mode, s_amp, s_phase, s_corr, s_live, (unsigned long)s_dropped, s_presence ? 1 : 0);
}

// One $T line per transmitter being modelled (include/sense_proto.h): what
// the screen needs to put each path on a radar. Once a second, after fuse().
static void sendTargets(uint32_t now) {
  for (int i = 0; i < kMaxTargets; i++) {
    if (!s_targets[i].used) continue;
    const CsiMotionModel& m = s_targets[i].model;
    outf("$T,%d,%02X%02X,%d,%u,%u,%u,%u\n", i, s_targets[i].mac[4], s_targets[i].mac[5], (int)m.rssi(),
         (unsigned)m.samplesPerSecond(), m.calibrated() ? m.motionScore() : 0, m.calibrationPercent(now), m.reliability());
  }
}

static void sendInfo() {
  // From eFuse, not WiFi.macAddress(): before the radio starts that returns
  // whatever was in the buffer (the first boot printed a stranger's address).
  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  outf("$I,%s,%02X:%02X:%02X:%02X:%02X:%02X,%u,%u,%s,%lu\n", FIRMWARE_VERSION, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
       s_mode, s_chan, s_ssid[0] ? s_ssid : "-", (unsigned long)ESP.getFreeHeap());
}

static void sendRates() {
  note("seen %lu, queued %lu, dropped %lu, heap %lu", (unsigned long)s_seen, (unsigned long)s_accepted, (unsigned long)s_dropped,
       (unsigned long)ESP.getFreeHeap());
  for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used) {
    const CsiMotionModel& m = s_targets[i].model;
    note("target %d %02X:%02X:%02X:%02X:%02X:%02X %u/s rssi %d tones %u cal %u%% rel %u score %u (a%u p%u c%u) live %.2f",
         i, s_targets[i].mac[0], s_targets[i].mac[1], s_targets[i].mac[2], s_targets[i].mac[3], s_targets[i].mac[4],
         s_targets[i].mac[5], m.samplesPerSecond(), m.rssi(), (unsigned)m.carriers(), m.calibrationPercent(millis()),
         m.reliability(), m.motionScore(), m.amplitudeScore(), m.phaseScore(), m.correlationScore(), m.live());
  }
}

// ---- Commands -------------------------------------------------------------
static void saveSettings() {
  if (s_vitals) return;   // a VITALS session's network is borrowed, never saved
  s_prefs.putString("ssid", s_ssid); s_prefs.putString("pass", s_pass);
  s_prefs.putUShort("chan", s_chan); s_prefs.putUInt("win", s_windowMs); s_prefs.putUInt("cal", s_calMs);
  s_prefs.putUInt("hold", s_holdMs);
  s_prefs.putUChar("mode", s_mode == SENSE_MODE_STA || s_mode == SENSE_MODE_JOINING ? SENSE_MODE_STA : SENSE_MODE_PARK);
}

static void recalibrate() {
  const uint32_t now = millis();
  for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used) {
    s_targets[i].accepted = s_targets[i].dropped = 0;
    s_targets[i].model.reset(now, s_calMs, s_windowMs);
  }
  s_wr = s_rd = 0;
  s_motionVotes = s_stillVotes = 0; s_state = SENSE_STATE_CAL; s_score = 0; s_presence = false;
  note("recalibrating: keep the room still for %lu s", (unsigned long)(s_calMs / 1000));
}

static void help() {
  note("commands: park <chan>|auto | join <ssid> <pass> | forget | scan | cal | win <ms> | caltime <ms> | hold <ms> | vitalsnet <ssid>TAB<pass> | vitals on [<ssid>\\t<pass>] | vitals off | info | rates | raw 0|1 | download | help");
}

// Every access point in range on both bands, loudest first, and the loudest
// channel on each band: where a park has the most to listen to. Sensing
// stops for the few seconds the scan takes and resumes as it was.
//
// pick (first boot with no channel saved, or "park auto"): park on the loudest
// 5 GHz access point's channel, else the loudest 2.4 GHz one, and save it,
// without listing every network. A fresh radio used to park on channel 1,
// where 2.4 GHz beacons carry no CSI: about one reading a second on the bench
// against ~49 a second on a 5 GHz channel (2026-11-07), so a new board showed
// LOW SIGNAL until someone typed "park" on its serial port.
static void scan(bool pick = false) {
  const uint8_t was = pick ? SENSE_MODE_PARK : s_mode;
  csiStop();
  if (s_wifiUp) esp_wifi_set_promiscuous(false);
  if (!s_wifiUp) radioCommon();
  note("scanning both bands...");
  const int n = WiFi.scanNetworks(false, true);
  if (n <= 0) note("scan: nothing found (%d)", n);
  int order[64];
  const int m = n > 64 ? 64 : (n < 0 ? 0 : n);
  for (int i = 0; i < m; i++) order[i] = i;
  for (int i = 1; i < m; i++) { int k = order[i], j = i; while (j && WiFi.RSSI(order[j - 1]) < WiFi.RSSI(k)) { order[j] = order[j - 1]; j--; } order[j] = k; }
  int best24 = -1, best5 = -1;
  for (int r = 0; r < m; r++) {
    const int i = order[r];
    const int ch = WiFi.channel(i);
    const uint8_t* b = WiFi.BSSID(i);
    if (ch <= 14 && best24 < 0) best24 = i;
    if (ch > 14 && best5 < 0) best5 = i;
    if (!pick) note("ap %2d  ch %3d  %s GHz  %4d dBm  %02X:%02X:%02X:%02X:%02X:%02X  %s", r, ch, ch > 14 ? "5  " : "2.4", WiFi.RSSI(i),
         b[0], b[1], b[2], b[3], b[4], b[5], WiFi.SSID(i).length() ? WiFi.SSID(i).c_str() : "(hidden)");
  }
  if (best24 >= 0) note("loudest 2.4 GHz: channel %d at %d dBm", WiFi.channel(best24), WiFi.RSSI(best24));
  if (best5 >= 0) note("loudest 5 GHz: channel %d at %d dBm", WiFi.channel(best5), WiFi.RSSI(best5));
  if (best5 < 0) note("no 5 GHz access point heard");
  const int chosen = best5 >= 0 ? WiFi.channel(best5) : (best24 >= 0 ? WiFi.channel(best24) : 0);
  WiFi.scanDelete();
  if (pick) {
    if (chosen > 0) {
      note("auto: parking on channel %d, the loudest network heard", chosen);
      if (startPark((uint16_t)chosen)) saveSettings();
    } else {
      // Nothing heard: park on the default, save nothing, so the next boot tries again.
      note("auto: no access point heard; channel %u for now", s_chan);
      startPark(s_chan);
    }
    return;
  }
  if (was == SENSE_MODE_STA || was == SENSE_MODE_JOINING) startJoin();
  else startPark(s_chan);
}

// Reboot straight into the ROM's serial download mode, so a new image can be
// written with no buttons (this board's USB chip cannot reset it). The same
// register JanOS's own `download` uses on the C5.
static void downloadMode() {
  note("rebooting into download mode: flash now, then RESET (or esptool's watchdog reset) to run");
  Serial.flush(); Serial0.flush();
  csiStop();
  esp_wifi_stop();
  delay(50);
  uint32_t cfg = REG_READ(LP_AON_SYS_CFG_REG);
  cfg &= ~LP_AON_FORCE_DOWNLOAD_BOOT_M;
  cfg |= (1U << LP_AON_FORCE_DOWNLOAD_BOOT_S);
  REG_WRITE(LP_AON_SYS_CFG_REG, cfg);
  esp_rom_software_reset_system();
}

static void command(char* line) {
  while (*line == ' ') line++;
  // The Tab5's own console lines ("[sense] ...", "[det] ...") can reach us
  // while it has let go of the bus, or as it boots: not commands, and not
  // worth a help listing each.
  if (*line == '[') return;
  char* sp = strchr(line, ' ');
  char* arg = nullptr;
  if (sp) { *sp = 0; arg = sp + 1; while (*arg == ' ') arg++; }
  if (!strcmp(line, "park")) {
    if (arg && !strcmp(arg, "auto")) { scan(true); return; }
    const int ch = arg ? atoi(arg) : s_chan;
    if (ch < 1) { note("park: which channel?"); return; }
    if (startPark((uint16_t)ch)) saveSettings();
  } else if (!strcmp(line, "join")) {
    if (!arg) { note("join <ssid> <pass>"); return; }
    char* sp2 = strchr(arg, ' ');
    if (sp2) { *sp2 = 0; strncpy(s_pass, sp2 + 1, sizeof s_pass - 1); } else s_pass[0] = 0;
    strncpy(s_ssid, arg, sizeof s_ssid - 1);
    if (startJoin()) saveSettings();
  } else if (!strcmp(line, "vitals")) {
    if (arg && !strncmp(arg, "on", 2)) {
      if (!s_vitals) { s_vitPrevMode = (s_mode == SENSE_MODE_STA || s_mode == SENSE_MODE_JOINING) ? SENSE_MODE_STA : SENSE_MODE_PARK; s_vitPrevChan = s_chan; }
      s_vitals = true; s_vit.reset(); s_vitTarget = -1; s_vitFrames = 0;
      const char* rest = arg + 2; while (*rest == ' ') rest++;
      const char* tab = strchr(rest, '\t');
      if (*rest && tab) {
        const size_t ln = (size_t)(tab - rest) < sizeof s_ssid - 1 ? (size_t)(tab - rest) : sizeof s_ssid - 1;
        memcpy(s_ssid, rest, ln); s_ssid[ln] = 0;
        strncpy(s_pass, tab + 1, sizeof s_pass - 1); s_pass[sizeof s_pass - 1] = 0;
        note("vitals on: joining \"%s\" for the session", s_ssid);
        startJoin();
      } else if (s_vssid[0]) {
        memcpy(s_ssid, s_vssid, sizeof s_ssid); memcpy(s_pass, s_vpass, sizeof s_pass);
        note("vitals on: joining \"%s\" (the VITALS network) for the session", s_ssid);
        startJoin();
      } else {
        note("vitals on: no network given, following the busiest transmitter");
        if (s_mode == SENSE_MODE_STA && !s_ping) startPing();
      }
    } else {
      const bool was = s_vitals;
      s_vitals = false;
      stopPing();
      if (was) {
        s_prefs.getString("ssid", s_ssid, sizeof s_ssid);
        s_prefs.getString("pass", s_pass, sizeof s_pass);
        note("vitals off: back to %s", s_vitPrevMode == SENSE_MODE_STA && s_ssid[0] ? "the saved network" : "listening");
        if (s_vitPrevMode == SENSE_MODE_STA && s_ssid[0]) startJoin(); else startPark(s_vitPrevChan);
      }
    }
  } else if (!strcmp(line, "vitalsnet")) {
    if (!arg || !strcmp(arg, "forget")) {
      s_vssid[0] = s_vpass[0] = 0; s_prefs.remove("vssid"); s_prefs.remove("vpass");
      note("vitals network forgotten"); return;
    }
    const char* tab = strchr(arg, '\t');
    if (!tab) { note("vitalsnet <ssid>TAB<pass>  (a tab between them, so names with spaces work)"); return; }
    const size_t ln = (size_t)(tab - arg) < sizeof s_vssid - 1 ? (size_t)(tab - arg) : sizeof s_vssid - 1;
    memcpy(s_vssid, arg, ln); s_vssid[ln] = 0;
    strncpy(s_vpass, tab + 1, sizeof s_vpass - 1); s_vpass[sizeof s_vpass - 1] = 0;
    s_prefs.putString("vssid", s_vssid); s_prefs.putString("vpass", s_vpass);
    note("vitals network saved: \"%s\"", s_vssid);
  } else if (!strcmp(line, "forget")) {
    s_ssid[0] = s_pass[0] = 0; saveSettings();
    startPark(s_chan > 14 ? s_chan : s_chan);
    note("network forgotten");
  } else if (!strcmp(line, "cal")) {
    recalibrate();
  } else if (!strcmp(line, "win")) {
    const long v = arg ? atol(arg) : 0;
    if (v < 200 || v > 5000) { note("win 200..5000 ms (now %lu)", (unsigned long)s_windowMs); return; }
    s_windowMs = (uint32_t)v; saveSettings(); recalibrate();
  } else if (!strcmp(line, "hold")) {
    const long v = arg ? atol(arg) : -1;
    if (v < 0 || v > 120000) { note("hold 0..120000 ms (now %lu)", (unsigned long)s_holdMs); return; }
    s_holdMs = (uint32_t)v; saveSettings(); note("presence hold %lu ms", (unsigned long)s_holdMs);
  } else if (!strcmp(line, "caltime")) {
    const long v = arg ? atol(arg) : 0;
    if (v < 5000 || v > 120000) { note("caltime 5000..120000 ms (now %lu)", (unsigned long)s_calMs); return; }
    s_calMs = (uint32_t)v; saveSettings(); recalibrate();
  } else if (!strcmp(line, "info")) {
    sendInfo();
  } else if (!strcmp(line, "rates")) {
    sendRates();
  } else if (!strcmp(line, "scan")) {
    scan();
  } else if (!strcmp(line, "download")) {
    downloadMode();
  } else if (!strcmp(line, "raw")) {
    s_raw = arg ? (uint8_t)atoi(arg) : 0;
    note("raw %u", s_raw);
  } else if (!strcmp(line, "help") || !strcmp(line, "?")) {
    help();
  } else if (*line) {
    note("unknown: %s", line); help();
  }
}

static void readPort(Stream& port, char* buf, size_t& len, size_t cap) {
  while (port.available()) {
    const char c = (char)port.read();
    if (c == '\n' || c == '\r') {
      if (len) { buf[len] = 0; command(buf); len = 0; }
    } else if (len < cap - 1) {
      buf[len++] = c;
    } else len = 0;   // a line too long is thrown away
  }
}

// ---- Arduino --------------------------------------------------------------
void setup() {
  Serial.begin(SENSE_BAUD);
  Serial.setTxTimeoutMs(0);   // native USB: never wait for a host
  Serial0.begin(SENSE_BAUD, SERIAL_8N1, SENSE_C5_RX, SENSE_C5_TX);
  delay(300);
  s_prefs.begin("sense", false);
  s_prefs.getString("ssid", s_ssid, sizeof s_ssid);
  s_prefs.getString("pass", s_pass, sizeof s_pass);
  s_chan = s_prefs.getUShort("chan", 1);
  s_windowMs = s_prefs.getUInt("win", 1000);
  s_calMs = s_prefs.getUInt("cal", 30000);
  s_holdMs = s_prefs.getUInt("hold", 15000);
  s_prefs.getString("vssid", s_vssid, sizeof s_vssid);
  s_prefs.getString("vpass", s_vpass, sizeof s_vpass);
  if (!s_vit.begin()) note("vitals: no memory for the chest signal");
  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
    if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) s_discReason = info.wifi_sta_disconnected.reason;
    if (s_mode == SENSE_MODE_JOINING) s_lastEvent = (uint16_t)e;
  });
  const uint8_t mode = s_prefs.getUChar("mode", SENSE_MODE_PARK);
  note("Tab5 Monster C5 CSI Human Sensing radio %s on %s, heap %lu, psram %lu", FIRMWARE_VERSION, ESP.getChipModel(),
       (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getPsramSize());
  sendInfo();
  if (mode == SENSE_MODE_STA && s_ssid[0]) startJoin();
  else if (!s_prefs.isKey("chan")) scan(true);   // first boot: find a channel worth listening to
  else startPark(s_chan);
  help();
}

void loop() {
  const uint32_t now = millis();
  static char usbBuf[128], uartBuf[128];
  static size_t usbLen = 0, uartLen = 0;
  readPort(Serial, usbBuf, usbLen, sizeof usbBuf);
  readPort(Serial0, uartBuf, uartLen, sizeof uartBuf);

  // Joining: wait for the link, give up after 20 s and park on the saved channel.
  if (s_mode == SENSE_MODE_JOINING) {
    // millis() afresh: `now` was read before this pass's commands, so a join
    // started by one of them is "later" than now, and now - start wrapped to
    // ~49 days: every join gave up in the pass it began (2026-11-08).
    const uint32_t jt = millis() - s_joinStartedMs;
    if (WiFi.status() == WL_CONNECTED) onJoined();
    else if (jt > 20000) {
      note("join failed (status %d, reason %u %s); parking on channel %u", (int)WiFi.status(), (unsigned)s_discReason, reasonText(s_discReason), s_chan);
      startPark(s_chan);
    }
  }
  if (s_mode == SENSE_MODE_STA) {
    if (WiFi.status() != WL_CONNECTED) {
      note("link lost, rejoining");
      startJoin();
    } else if (!s_vitals && now - s_lastPingMs >= kPingMs) {
      s_lastPingMs = now;
      // A small datagram to the gateway's echo port: whatever it answers
      // (an echo, or ICMP saying no), the answer is a frame from the AP,
      // and so is the ACK of ours. That is the reference traffic.
      static uint8_t pad[32];
      s_udp.beginPacket(WiFi.gatewayIP(), 7);
      s_udp.write(pad, sizeof pad);
      s_udp.endPacket();
    }
  }

  // Drain the queue into the models.
  while (s_rd != s_wr) {
    Sample& s = s_queue[s_rd];
    const int t = s_lockTarget ? (memcmp(s.mac, s_lockMac, 6) == 0 ? 0 : -1) : findTarget(s.mac);
    if (t >= 0) {
      if (s_vitals && t == s_vitTarget) s_vit.addSample(s.iq, s.length, s.first_word_invalid);
      s_targets[t].accepted++;
      s_targets[t].lastAt = s.at;
      static uint32_t s_raw3At = 0;
      if (s_raw == 3 && s.at - s_raw3At >= 200) {
        // Every 16-bit word of the sample decoded as a signed 12-bit value,
        // five samples a second: for working out the tone layout offline.
        s_raw3At = s.at;
        char b[SENSE_LINE_MAX * 3];
        int n = snprintf(b, sizeof b, "# csi3 %02X%02X len %u:", s.mac[4], s.mac[5], (unsigned)s.length);
        for (int i = 0; i + 1 < s.length && n < (int)sizeof b - 8; i += 2) {
          uint16_t w = (uint8_t)s.iq[i] | ((uint16_t)(uint8_t)s.iq[i + 1] << 8);
          if (w & 0x0800) w |= 0xF000;
          n += snprintf(b + n, sizeof b - n, " %d", (int)(int16_t)w);
        }
        snprintf(b + n, sizeof b - n, "\n");
        out(b);
      }
      if (s_raw == 2) {
        // Of the 16-bit words in each half, how many look like a 12-bit
        // sample (top byte 0x00 or 0x0F)? Real data scores near 100 %.
        const int half = s.length / 2;
        int a = 0, an = 0, b = 0, bn = 0;
        for (int i = 0; i + 1 < s.length; i += 2) {
          const uint8_t hi = (uint8_t)s.iq[i + 1];
          const bool like = hi == 0x00 || hi == 0x0F;
          if (i < half) { an++; a += like; } else { bn++; b += like; }
        }
        outf("# csi2 len %u copied %u  first half %d/%d 12-bit-like, second half %d/%d\n",
             (unsigned)(s.length / 2), (unsigned)s.length, a, an, b, bn);
      } else {
        s_targets[t].model.addSample(s.iq, s.length, s.rssi, s.at, s.first_word_invalid, CsiMotionModel::SampleEncoding::SIGNED_12_LE);
      }
      if (s_raw == 1) {
        char b[160];
        int n = snprintf(b, sizeof b, "# csi %02X:%02X:%02X:%02X:%02X:%02X ch%u rssi %d len %u fwi %u", s.mac[0], s.mac[1], s.mac[2], s.mac[3],
                         s.mac[4], s.mac[5], s.channel, s.rssi, s.length, s.first_word_invalid);
        for (int i = 0; i < 8 && n < (int)sizeof b - 8; i++) n += snprintf(b + n, sizeof b - n, " %d", (int)s.iq[i]);
        snprintf(b + n, sizeof b - n, "\n");
        out(b);
      }
    }
    s_rd = (uint8_t)((s_rd + 1) % kQueueSize);
  }

  if (now - s_lastRankMs >= kRankMs) { s_lastRankMs = now; rankSources(now); }
  if (now - s_lastFusionMs >= 1000) {
    s_lastFusionMs = now; fuse(now); sendTargets(now);
    if (s_vitals) {
      // Joined: the router. Parked: the busiest transmitter. A change of
      // transmitter starts the chest signal over.
      // Parked, the mesh nodes take turns being busiest; switching on every
      // turn restarted the signal each second (2026-11-08). Stay with the
      // current one unless it went quiet, or another is half as busy again
      // for five seconds running.
      static uint8_t wins = 0;
      int best = -1; uint16_t bestRate = 0;
      const bool curOk = s_vitTarget >= 0 && s_targets[s_vitTarget].used && memcmp(s_vitMac, s_targets[s_vitTarget].mac, 6) == 0;
      if (s_lockTarget) best = 0;
      else {
        for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used && s_targets[i].model.samplesPerSecond() > bestRate) { bestRate = s_targets[i].model.samplesPerSecond(); best = i; }
        if (curOk && best != s_vitTarget) {
          const uint16_t cur = s_targets[s_vitTarget].model.samplesPerSecond();
          wins = bestRate * 2 > cur * 3 ? (uint8_t)(wins + 1) : 0;
          if (wins < 5) best = s_vitTarget;
        } else wins = 0;
      }
      if (best >= 0 && (best != s_vitTarget || memcmp(s_vitMac, s_targets[best].mac, 6) != 0)) {
        wins = 0;
        s_vitTarget = best; memcpy(s_vitMac, s_targets[best].mac, 6); s_vit.reset();
        note("vitals: chest signal from %02X:%02X:%02X:%02X:%02X:%02X", s_vitMac[0], s_vitMac[1], s_vitMac[2], s_vitMac[3], s_vitMac[4], s_vitMac[5]);
      }
    }
    if (s_state == SENSE_STATE_MOTION) { s_presence = true; s_lastMotionMs = now; }
    else if (s_presence && (now - s_lastMotionMs > s_holdMs || s_state == SENSE_STATE_CAL || s_state == SENSE_STATE_LOWDATA)) s_presence = false;
  }
  if (now - s_lastLineMs >= 1000 / SENSE_LINE_HZ) {
    s_lastLineMs = now;
    // The live value moves between fusions; refresh it from the best target.
    float lv = 0.0f;
    for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used && s_targets[i].model.calibrated() && s_targets[i].model.live() > lv) lv = s_targets[i].model.live();
    lv *= 100.0f; if (lv > 1000.0f) lv = 1000.0f; s_live = (uint16_t)lv;
    sendStatus(now);
  }
  if (s_vitals) {
    float c1, c2; uint8_t n, q;
    if (s_vit.tick(now, c1, c2, n, q)) {
      s_vitFrames++;
      outf("$V,%lu,%ld,%ld,%u,%u\n", (unsigned long)now, lroundf(c1 * 10000.0f), lroundf(c2 * 10000.0f), (unsigned)n, (unsigned)q);
    }
  }
  if (now - s_lastInfoMs >= kInfoMs) { s_lastInfoMs = now; sendInfo(); }
  delay(s_vitals ? 1 : 2);
}
#endif  // SENSE_C5
