// Tilt controllers (ESP32 + ADXL345 "Marble-P1"/"Marble-P2", Bluetooth LE) and portrait versions of
// Tilt Arcade games: Dodge, Fruit Catch (1P/VS), Highway, Star Shooter (solo/co-op), Cave Flyer (1P/VS), Pong.
// Included from PCMonitorX_S3_TFT28.ino after the drawing helpers.
#pragma once
#include <BLEDevice.h>

// =============================================================== controllers
namespace pads {
const char *SVC_UUID = "6e400001-7a3b-4c1d-9e8f-0123456789ab";
const char *CHR_UUID = "6e400002-7a3b-4c1d-9e8f-0123456789ab";
struct Pad {
  volatile int16_t rx = 0, ry = 0;  // milli-g, as sent by the controller
  volatile uint32_t at = 0;
  volatile bool linked = false;
  BLEClient *cli = nullptr;
};
Pad pad[2];  // [0] = board Marble-P1, [1] = board Marble-P2
volatile bool scanning = false;

bool connected(int b) { return pad[b].linked && millis() - pad[b].at < 600; }
int boardOf(int p) { return p == 1 ? st.p1Board : 1 - st.p1Board; }
// Board tilt in g with that board's hold direction / invert settings applied (same rules as tiltlib.board_tilt).
bool boardTilt(int b, float &x, float &y) {
  if (!connected(b)) return false;
  x = pad[b].rx / 1000.0f;
  y = pad[b].ry / 1000.0f;
  float t;
  switch (st.padHold[b]) {
    case 1: t = x; x = -y; y = t; break;   // sideways left
    case 2: t = x; x = y; y = -t; break;   // sideways right
    case 3: x = -x; y = -y; break;         // upside down
  }
  if (st.padInvX[b]) x = -x;
  if (st.padInvY[b]) y = -y;
  return true;
}
bool tilt(int p, float &x, float &y) {
  if (!boardTilt(boardOf(p), x, y)) return false;
  if (fabsf(x) < 0.03f) x = 0;
  if (fabsf(y) < 0.03f) y = 0;
  return true;
}
bool any() { return connected(0) || connected(1); }
void strongest(float &x, float &y) {
  x = y = 0;
  float a, b;
  for (int p = 1; p <= 2; p++)
    if (tilt(p, a, b) && fabsf(a) + fabsf(b) > fabsf(x) + fabsf(y)) { x = a; y = b; }
}

bool connectPad(int b, BLEAdvertisedDevice &d) {
  if (!pad[b].cli) pad[b].cli = BLEDevice::createClient();
  BLEClient *c = pad[b].cli;
  if (!c->connect(&d)) { Serial.printf("[pad] P%d connect failed\n", b + 1); return false; }
  BLERemoteService *s = c->getService(BLEUUID(SVC_UUID));
  BLERemoteCharacteristic *ch = s ? s->getCharacteristic(BLEUUID(CHR_UUID)) : nullptr;
  if (!ch || !ch->canNotify()) {
    Serial.printf("[pad] P%d has no tilt characteristic\n", b + 1);
    c->disconnect();
    return false;
  }
  ch->registerForNotify([b](BLERemoteCharacteristic *, uint8_t *data, size_t n, bool) {
    if (n >= 4) {
      pad[b].rx = (int16_t)(data[0] | data[1] << 8);
      pad[b].ry = (int16_t)(data[2] | data[3] << 8);
      pad[b].at = millis();
    }
  });
  pad[b].at = millis();
  pad[b].linked = true;
  Serial.printf("[pad] P%d connected\n", b + 1);
  return true;
}

void task(void *) {
  BLEDevice::init("PCMonitorX");
  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);  // the controller's name is in the scan response
  scan->setInterval(160);
  scan->setWindow(60);        // leave radio time for WiFi
  for (;;) {
    for (int b = 0; b < 2; b++) {
      if (pad[b].linked && !(pad[b].cli && pad[b].cli->isConnected())) {
        pad[b].linked = false;
        Serial.printf("[pad] P%d disconnected\n", b + 1);
      }
      if (!st.padsOn && pad[b].linked) pad[b].cli->disconnect();
    }
    if (!st.padsOn || (pad[0].linked && pad[1].linked)) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    scanning = true;
    BLEScanResults *res = scan->start(2, false);
    scanning = false;
    int n = res ? res->getCount() : 0;
    for (int i = 0; i < n; i++) {
      BLEAdvertisedDevice d = res->getDevice(i);
      String nm = d.getName();
      int b = nm == "Marble-P1" ? 0 : nm == "Marble-P2" ? 1 : -1;
      if (b >= 0 && !pad[b].linked) connectPad(b, d);
    }
    scan->clearResults();
    vTaskDelay(pdMS_TO_TICKS(pad[0].linked || pad[1].linked ? 4000 : 2000));
  }
}
}  // namespace pads

// =============================================================== shared tilt-game helpers
namespace tg {
const int FY = APP_Y + 20, FW = W, FH = H - FY;  // playfield 240 x 276 below a 20 px HUD
const uint16_t BLUE = rgb(70, 140, 250), RED = rgb(235, 70, 70), GOLD = rgb(250, 200, 60), FIELD = rgb(34, 38, 54);
bool wantExit = false;  // set by a game's end screen; acted on after the frame is drawn
uint16_t pcol(int p) { return p == 1 ? BLUE : RED; }
const char *pname(int p) { return p == 1 ? "BLUE" : "RED"; }
uint16_t mix(uint16_t a, uint16_t b, float t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31, br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)(((int)(ar + (br - ar) * t) << 11) | ((int)(ag + (bg - ag) * t) << 5) | (int)(ab + (bb - ab) * t));
}
uint16_t lighten(uint16_t c, float t = 0.4f) { return mix(c, 0xFFFF, t); }
uint32_t lastT = 0;
float frameDt() {
  uint32_t now = millis();
  float dt = lastT ? (now - lastT) / 1000.0f : 0;
  lastT = now;
  return fminf(dt, 1 / 20.0f);
}
float frand(float a, float b) { return a + (b - a) * (esp_random() / 4294967295.0f); }
int SX(float x) { return (int)lroundf(x); }
int SY(float y) { return FY + (int)lroundf(y); }

// Without a controller, steer toward the finger. half: 0 anywhere, 1 bottom, 2 top, 3 left, 4 right.
bool touchToward(float px, float py, float &x, float &y, int half) {
  if (!T.down || T.ignore || T.sy < FY) return false;
  int ty = T.y - FY;
  if ((half == 1 && ty < FH / 2) || (half == 2 && ty >= FH / 2) || (half == 3 && T.x >= W / 2) || (half == 4 && T.x < W / 2))
    return false;
  x = constrain((T.x - px) / 40.0f, -1.2f, 1.2f);
  y = constrain((ty - py) / 40.0f, -1.2f, 1.2f);
  return true;
}
void soloTilt(float px, float py, float &x, float &y) {
  if (pads::tilt(1, x, y) || pads::tilt(2, x, y)) return;
  if (!touchToward(px, py, x, y, 0)) x = y = 0;
}
void playerTilt(int p, float px, float py, float &x, float &y, int half) {
  if (pads::tilt(p, x, y)) return;
  if (!touchToward(px, py, x, y, half)) x = y = 0;
}

struct Ball {
  float x, y, vx, vy, r, hx, hy, falling;
  uint16_t col;
  int p, tn;
  float tx[8], ty[8];
  void init(float X, float Y, float R, uint16_t c, int P) { hx = X; hy = Y; r = R; col = c; p = P; respawn(); }
  void respawn() { x = hx; y = hy; vx = vy = 0; falling = 0; tn = 0; }
  void trail(int maxN = 8) {
    if (tn < maxN) tn++;
    for (int i = tn - 1; i > 0; i--) { tx[i] = tx[i - 1]; ty[i] = ty[i - 1]; }
    tx[0] = x; ty[0] = y;
  }
};
void integrate(Ball &b, float tx, float ty, float h, float accel = 420, float friction = 1.2f, float maxSp = 260) {
  b.vx += tx * accel * h;
  b.vy += ty * accel * h;
  float f = fmaxf(0.0f, 1.0f - friction * h);
  b.vx *= f;
  b.vy *= f;
  float sp = hypotf(b.vx, b.vy);
  if (sp > maxSp) { b.vx *= maxSp / sp; b.vy *= maxSp / sp; }
  b.x += b.vx * h;
  b.y += b.vy * h;
}
void collideRect(Ball &b, float rx, float ry, float rw, float rh, float bounce = 0.45f) {
  float cx = fminf(fmaxf(b.x, rx), rx + rw), cy = fminf(fmaxf(b.y, ry), ry + rh);
  float dx = b.x - cx, dy = b.y - cy, d2 = dx * dx + dy * dy, nx, ny, pen;
  if (d2 >= b.r * b.r) return;
  if (d2 > 1e-9f) {
    float d = sqrtf(d2);
    nx = dx / d; ny = dy / d; pen = b.r - d;
  } else {
    float opt[4] = {b.x - rx, rx + rw - b.x, b.y - ry, ry + rh - b.y};
    int k = 0;
    for (int i = 1; i < 4; i++) if (opt[i] < opt[k]) k = i;
    nx = k == 0 ? -1 : k == 1 ? 1 : 0;
    ny = k == 2 ? -1 : k == 3 ? 1 : 0;
    pen = opt[k] + b.r;
  }
  b.x += nx * pen;
  b.y += ny * pen;
  float vn = b.vx * nx + b.vy * ny;
  if (vn < 0) { b.vx -= (1 + bounce) * vn * nx; b.vy -= (1 + bounce) * vn * ny; }
}
float collideMass(Ball &a, Ball &b, float ma, float mb, float e) {
  if (a.falling > 0 || b.falling > 0) return 0;
  float dx = b.x - a.x, dy = b.y - a.y, rr = a.r + b.r, d2 = dx * dx + dy * dy;
  if (d2 >= rr * rr || d2 < 1e-9f) return 0;
  float d = sqrtf(d2), nx = dx / d, ny = dy / d, ov = rr - d;
  a.x -= nx * ov * mb / (ma + mb); a.y -= ny * ov * mb / (ma + mb);
  b.x += nx * ov * ma / (ma + mb); b.y += ny * ov * ma / (ma + mb);
  float rel = (b.vx - a.vx) * nx + (b.vy - a.vy) * ny;
  if (rel >= 0) return 0;
  float j = -(1 + e) * rel / (1 / ma + 1 / mb);
  a.vx -= j / ma * nx; a.vy -= j / ma * ny;
  b.vx += j / mb * nx; b.vy += j / mb * ny;
  return -rel;
}
void drawBall(const Ball &b, float scale = 1, uint16_t bg = FIELD) {
  float r = b.r * scale;
  if (r <= 0.3f) return;
  for (int i = b.tn - 1; i >= 0; i--) {
    float k = (b.tn - i) / (b.tn + 1.0f);
    g->fillCircle(SX(b.tx[i]), SY(b.ty[i]), fmaxf(1.0f, b.r * (0.4f + 0.5f * k)), mix(bg, b.col, 0.35f * k));
  }
  g->fillCircle(SX(b.x + r * 0.2f), SY(b.y + r * 0.25f), r, rgb(10, 10, 15));
  g->fillCircle(SX(b.x), SY(b.y), r, b.col);
  g->fillCircle(SX(b.x - r * 0.35f), SY(b.y - r * 0.35f), fmaxf(1.0f, r * 0.35f), lighten(b.col));
}

struct Spark {
  float x, y, vx, vy, ttl;
  uint16_t col;
};
struct Sparks {
  Spark s[120];
  int n = 0;
  void clear() { n = 0; }
  void add(float x, float y, float vx, float vy, float ttl, uint16_t c) {
    if (n < 120) s[n++] = {x, y, vx, vy, ttl, c};
  }
  void burst(float x, float y, uint16_t c, int cnt = 14, float sp = 110) {
    for (int i = 0; i < cnt; i++) {
      float a = frand(0, TWO_PI), v = frand(20, sp);
      add(x, y, cosf(a) * v, sinf(a) * v, frand(0.3f, 0.7f), c);
    }
  }
  void step(float dt) {
    int j = 0;
    for (int i = 0; i < n; i++) {
      Spark &p = s[i];
      p.x += p.vx * dt; p.y += p.vy * dt; p.ttl -= dt;
      if (p.ttl > 0) s[j++] = p;
    }
    n = j;
  }
  void draw(int r = 1) {
    for (int i = 0; i < n; i++) g->fillCircle(SX(s[i].x), SY(s[i].y), r, s[i].col);
  }
};

void panel(int y0 = 108, int h = 124) { g->fillRoundRect(8, y0, W - 16, h, 10, rgb(14, 16, 26)); g->drawRoundRect(8, y0, W - 16, h, 10, C_TRACK); }
void holdBar(float prog, int cx, int cy, const char *label, uint16_t col) {
  int w = 104, h = 18;
  g->fillRoundRect(cx - w / 2, cy - h / 2, w, h, 9, rgb(50, 54, 70));
  if (prog > 0) g->fillRoundRect(cx - w / 2, cy - h / 2, (int)(w * fminf(1.0f, prog)), h, 9, col);
  text(label, cx, cy, &fonts::Font0, C_TEXT, textdatum_t::middle_center);
}
// Game-over chooser: hold tilt RIGHT = play again, hold tilt LEFT = back to the apps. Tap also plays again.
struct EndChoice {
  float again = 0, menu = 0, lock = 1;
  void reset() { again = menu = 0; lock = 1; }
  int update(float dt) {
    lock -= dt;
    if (lock > 0) return 0;
    float x, y;
    pads::strongest(x, y);
    again = x > 0.4f ? again + dt : fmaxf(0.0f, again - 2 * dt);
    menu = x < -0.4f ? menu + dt : fmaxf(0.0f, menu - 2 * dt);
    if (again >= 0.9f) { reset(); return 1; }
    if (menu >= 0.9f) { reset(); return 2; }
    return 0;
  }
  void draw(int y) {
    if (pads::any()) {
      holdBar(menu / 0.9f, 64, y, "< tilt: menu", C_DIM);
      holdBar(again / 0.9f, 176, y, "tilt: again >", C_GOOD);
      text("or tap to play again", 120, y + 18, &fonts::Font0, C_DIM, textdatum_t::middle_center);
    } else {
      text("Tap to play again", 120, y, &fonts::Font2, C_TEXT, textdatum_t::middle_center);
    }
  }
};
void countdown(float timer, const char *sub, const char *go = "GO!") {
  int n = (int)ceilf(timer);
  char b[8];
  snprintf(b, sizeof(b), "%d", n);
  text(n > 0 ? b : go, 120, 150, &fonts::FreeSansBold24pt7b, C_TEXT, textdatum_t::middle_center);
  text(sub, 120, 196, &fonts::Font2, C_DIM, textdatum_t::middle_center);
  if (!pads::any()) text("no controller: steer with your finger", 120, 216, &fonts::Font0, C_WARN, textdatum_t::middle_center);
}
void hudSolo(const char *left, const char *mid, uint32_t best) {
  g->fillRect(0, APP_Y, W, FY - APP_Y, C_CARD);
  text(left, 6, APP_Y + 10, &fonts::Font2, C_TEXT, textdatum_t::middle_left);
  text(mid, 132, APP_Y + 10, &fonts::Font0, C_DIM, textdatum_t::middle_center);
  char b[20];
  if (best) snprintf(b, sizeof(b), "best %lu", (unsigned long)best);
  else snprintf(b, sizeof(b), "no best");
  text(b, W - 6, APP_Y + 10, &fonts::Font0, C_DIM, textdatum_t::middle_right);
}
void hud2(int s1, int s2, const char *mid) {
  g->fillRect(0, APP_Y, W, FY - APP_Y, C_CARD);
  char b[16];
  snprintf(b, sizeof(b), "BLUE %d", s1);
  text(b, 6, APP_Y + 10, &fonts::Font2, BLUE, textdatum_t::middle_left);
  snprintf(b, sizeof(b), "%d RED", s2);
  text(b, W - 6, APP_Y + 10, &fonts::Font2, RED, textdatum_t::middle_right);
  text(mid, 120, APP_Y + 10, &fonts::Font0, C_DIM, textdatum_t::middle_center);
  for (int p = 1; p <= 2; p++)  // small dot: filled = controller, ring = finger
    if (pads::connected(pads::boardOf(p))) g->fillCircle(p == 1 ? 74 : 166, APP_Y + 10, 3, pcol(p));
    else g->drawCircle(p == 1 ? 74 : 166, APP_Y + 10, 3, pcol(p));
}
void soloResult(const char *big, bool isNew, uint32_t best, EndChoice &end) {
  panel();
  text(big, 120, 136, &fonts::FreeSansBold12pt7b, GOLD, textdatum_t::middle_center);
  char b[24];
  snprintf(b, sizeof(b), "best: %lu", (unsigned long)best);
  text(isNew ? "NEW HIGH SCORE!" : b, 120, 164, &fonts::Font2, isNew ? GOLD : C_DIM, textdatum_t::middle_center);
  end.draw(196);
}
void matchResult(int winner, const char *line, EndChoice &end) {
  panel();
  char b[20];
  snprintf(b, sizeof(b), "%s WINS!", winner ? pname(winner) : "");
  text(winner ? b : "DRAW!", 120, 136, &fonts::FreeSansBold12pt7b, winner ? pcol(winner) : C_TEXT, textdatum_t::middle_center);
  text(line, 120, 164, &fonts::Font4, C_TEXT, textdatum_t::middle_center);
  end.draw(196);
}
// Full-screen chooser for modes/maps: tap a row (tap again to start), or tilt forward/back + hold RIGHT.
struct Picker {
  int sel = 0;
  float cool = 0, go = 0, lock = 0.6f;
  void reset() { sel = 0; cool = go = 0; lock = 0.6f; }
  int update(float dt, int n) {
    lock -= dt;
    cool -= dt;
    if (lock > 0) return -1;
    float x, y;
    pads::strongest(x, y);
    if (fabsf(y) > 0.35f && cool <= 0) { sel = (sel + (y > 0 ? 1 : -1) + n) % n; cool = 0.4f; go = 0; }
    else if (fabsf(y) < 0.2f) cool = fminf(cool, 0.0f);
    go = (x > 0.4f && fabsf(y) < 0.35f) ? go + dt : fmaxf(0.0f, go - 2 * dt);
    if (go >= 0.9f) { go = 0; return sel; }
    return -1;
  }
  void draw(const char *title, const char *const *names, const char *const *descs, int n) {
    g->fillRect(0, APP_Y, W, H - APP_Y, C_BG);
    text(title, 120, APP_Y + 22, &fonts::FreeSansBold12pt7b, C_TEXT, textdatum_t::middle_center);
    for (int i = 0; i < n; i++) {
      int y = APP_Y + 46 + i * 52;
      bool on = i == sel;
      g->fillRoundRect(10, y, 220, 46, 8, on ? rgb(52, 58, 82) : rgb(30, 33, 46));
      if (on && go > 0) g->fillRoundRect(10, y, (int)(220 * fminf(1.0f, go / 0.9f)), 46, 8, rgb(40, 120, 80));
      if (on) g->drawRoundRect(10, y, 220, 46, 8, GOLD);
      text(names[i], 22, y + 15, &fonts::Font2, C_TEXT, textdatum_t::middle_left);
      text(descs[i], 22, y + 33, &fonts::Font0, C_DIM, textdatum_t::middle_left);
    }
    text(pads::any() ? "tilt fwd/back to choose, hold RIGHT to play" : "tap to choose, tap again to play", 120, H - 12,
         &fonts::Font0, C_DIM, textdatum_t::middle_center);
  }
  int tap(int x, int y, int n) {
    int i = (y - APP_Y - 46) / 52;
    if (y < APP_Y + 46 || i < 0 || i >= n) return -1;
    if (i == sel) return sel;
    sel = i;
    return -1;
  }
};
}  // namespace tg

// =============================================================== Tilt Dodge (1 player)
namespace dodge {
using namespace tg;
const float PYP = FH - 22, PR = 7;
const uint16_t HUES[4] = {rgb(230, 90, 120), rgb(240, 150, 70), rgb(170, 110, 240), rgb(90, 200, 220)};
struct Blk { float x, y, w, h, sp; uint16_t col; } blk[40];
struct Coin { float x, y, sp; } coin[12];
struct Pop { float x, y, ttl; } pop[8];
int nb, nc, np, state;  // 0 countdown, 1 play, 2 over
float star[50][3], vx, t, score, spawn, timer, flash;
bool isNew;
Ball ball;
EndChoice end;
float speed() { return fminf(300.0f, 92.0f + 8.0f * t); }
void reset() {
  ball.init(120, PYP, PR, BLUE, 1);
  vx = 0; nb = nc = np = 0; t = score = 0; spawn = 0.6f; state = 0; timer = 3; flash = 0;
  for (auto &s : star) { s[0] = frand(0, FW); s[1] = frand(0, FH); s[2] = frand(0.3f, 1.0f); }
  end.reset();
}
void enter() { reset(); lastT = 0; }
void step(float dt) {
  flash = fmaxf(0.0f, flash - dt);
  float sp = state == 1 ? speed() : 40.0f;
  for (auto &s : star) {
    s[1] += sp * 0.35f * s[2] * dt;
    if (s[1] > FH) { s[1] -= FH; s[0] = frand(0, FW); }
  }
  for (int i = 0; i < np; i++) { pop[i].y -= 30 * dt; pop[i].ttl -= dt; }
  while (np && pop[0].ttl <= 0) { memmove(pop, pop + 1, sizeof(Pop) * --np); }
  if (state == 0) { timer -= dt; if (timer <= 0) state = 1; return; }
  if (state != 1) {
    int c = end.update(dt);
    if (c == 1) reset(); else if (c == 2) wantExit = true;
    return;
  }
  t += dt;
  score += dt * 10;
  float tx, ty;
  soloTilt(ball.x, PYP, tx, ty);
  float target = constrain(tx, -1.2f, 1.2f) * 250;
  vx += (target - vx) * fminf(1.0f, 8 * dt);
  ball.x += vx * dt;
  if (ball.x < PR) { ball.x = PR; vx = 0; }
  if (ball.x > FW - PR) { ball.x = FW - PR; vx = 0; }
  ball.y = PYP + 4;
  ball.trail();
  ball.y = PYP;
  spawn -= dt;
  if (spawn <= 0) {
    spawn = fmaxf(0.22f, 0.85f - 0.02f * t);
    float w = frand(12, 20 + fminf(30.0f, t * 0.75f));
    if (nb < 40) blk[nb++] = {frand(0, FW - w), -12, w, 10, sp * frand(0.85f, 1.2f), HUES[esp_random() % 4]};
    if (frand(0, 1) < 0.3f && nc < 12) coin[nc++] = {frand(8, FW - 8), -6, sp * 0.9f};
  }
  int j = 0;
  for (int i = 0; i < nb; i++) { blk[i].y += blk[i].sp * dt; if (blk[i].y < FH + 10) blk[j++] = blk[i]; }
  nb = j;
  j = 0;
  for (int i = 0; i < nc; i++) { coin[i].y += coin[i].sp * dt; if (coin[i].y < FH + 10) coin[j++] = coin[i]; }
  nc = j;
  for (int i = 0; i < nb; i++) {
    float cx = fminf(fmaxf(ball.x, blk[i].x), blk[i].x + blk[i].w), cy = fminf(fmaxf(PYP, blk[i].y), blk[i].y + blk[i].h);
    if ((ball.x - cx) * (ball.x - cx) + (PYP - cy) * (PYP - cy) < (PR - 1) * (PR - 1)) {
      state = 2;
      flash = 0.5f;
      isNew = hsBetter(HS_DODGE, (uint32_t)score, false);
      return;
    }
  }
  j = 0;
  for (int i = 0; i < nc; i++) {
    if (hypotf(coin[i].x - ball.x, coin[i].y - PYP) < PR + 5) {
      score += 25;
      if (np < 8) pop[np++] = {coin[i].x, coin[i].y, 0.8f};
    } else {
      coin[j++] = coin[i];
    }
  }
  nc = j;
}
void frame() {
  step(frameDt());
  const uint16_t bg = rgb(12, 14, 26);
  g->fillRect(0, FY, FW, FH, bg);
  for (auto &s : star) {
    int c = 60 + 120 * s[2];
    g->drawPixel(SX(s[0]), SY(s[1]), rgb(c, c, min(255, c + 20)));
  }
  for (int i = 0; i < nb; i++) {
    g->fillRoundRect(SX(blk[i].x), SY(blk[i].y), (int)blk[i].w, (int)blk[i].h, 2, blk[i].col);
    g->drawFastHLine(SX(blk[i].x), SY(blk[i].y), (int)blk[i].w, lighten(blk[i].col, 0.3f));
  }
  for (int i = 0; i < nc; i++) {
    g->fillCircle(SX(coin[i].x), SY(coin[i].y), 5, rgb(150, 110, 20));
    g->fillCircle(SX(coin[i].x), SY(coin[i].y), 4, GOLD);
  }
  drawBall(ball, 1, bg);
  for (int i = 0; i < np; i++) text("+25", SX(pop[i].x), SY(pop[i].y), &fonts::Font0, GOLD, textdatum_t::middle_center);
  char l[20], m[20];
  snprintf(l, sizeof(l), "%d", (int)score);
  snprintf(m, sizeof(m), "speed %.1fx", speed() / 92);
  hudSolo(l, m, hs[HS_DODGE]);
  if (flash > 0) for (int k = 0; k < 4; k++) g->drawRect(k, FY + k, FW - 2 * k, FH - 2 * k, C_BAD);
  if (state == 0) countdown(timer, "tilt left / right to steer");
  else if (state == 2) {
    snprintf(l, sizeof(l), "%d points", (int)score);
    soloResult(l, isNew, hs[HS_DODGE], end);
  }
}
void tap(int, int) { if (state == 2) reset(); }
}  // namespace dodge

// =============================================================== Fruit Catch (1 player or VS)
namespace fcatch {
using namespace tg;
const float ROUND = 60, BWID = 32, BYP = FH - 34, SPEED = 250;
struct Kind { int val; uint16_t col; float r; int w; };
const Kind KINDS[4] = {{1, rgb(220, 60, 60), 5.5f, 55}, {2, rgb(245, 150, 40), 6, 25}, {5, rgb(250, 210, 60), 6, 6}, {-5, rgb(30, 30, 36), 6.5f, 18}};
struct Item { float x, y, vy, spin; int k; } it[40];
struct Pop { float x, y, ttl; int val; } pops[10];
int ni, npop, state = -1, score[3], winner;  // state -1 pick, 0 countdown, 1 play, 2 over
float x[3], vx[3], stun[3], left, spawnT, t, timer;
bool solo = true, isNew;
EndChoice end;
Picker pick;
const char *MODES[] = {"1 player", "2 players (VS)"};
const char *MDESC[] = {"60 seconds, best score is saved", "Two baskets, one sky. Most points wins"};
void start(bool s) {
  solo = s;
  x[1] = solo ? 120 : 80; x[2] = 160;
  vx[1] = vx[2] = stun[1] = stun[2] = 0;
  score[1] = score[2] = 0;
  ni = npop = 0; left = ROUND; spawnT = 0.5f; t = 0; state = 0; timer = 2.5f; winner = 0;
  end.reset();
}
void enter() { state = -1; pick.reset(); lastT = 0; }
void drawItem(float px, float py, int k, float spin) {
  const Kind &K = KINDS[k];
  int cx = SX(px), cy = SY(py);
  if (k == 2) {  // star
    for (int i = 0; i < 10; i++) {
      float a0 = spin + i * PI / 5, a1 = spin + (i + 1) * PI / 5, r0 = i % 2 ? K.r * 0.45f : K.r, r1 = i % 2 ? K.r : K.r * 0.45f;
      g->fillTriangle(cx, cy, cx + cosf(a0) * r0, cy + sinf(a0) * r0, cx + cosf(a1) * r1, cy + sinf(a1) * r1, K.col);
    }
    return;
  }
  g->fillCircle(cx, cy, K.r, K.col);
  if (k == 3) {
    g->drawLine(cx + 2, cy - K.r + 1, cx + 4, cy - K.r - 3, rgb(140, 120, 90));
    if ((int)(spin * 4) % 2) g->fillCircle(cx + 4, cy - K.r - 3, 2, rgb(255, 200, 60));
  } else {
    g->fillCircle(cx - K.r * 0.35f, cy - K.r * 0.35f, 2, lighten(K.col, 0.3f));
    g->drawLine(cx, cy - K.r, cx + 1, cy - K.r - 3, rgb(90, 60, 30));
    if (k == 0) g->fillEllipse(cx + 3, cy - K.r - 2, 2, 1, rgb(80, 180, 80));
  }
}
void step(float dt) {
  t += dt;
  for (int i = 0; i < npop; i++) { pops[i].y -= 25 * dt; pops[i].ttl -= dt; }
  int j = 0;
  for (int i = 0; i < npop; i++) if (pops[i].ttl > 0) pops[j++] = pops[i];
  npop = j;
  if (state == 0) { timer -= dt; if (timer <= 0) state = 1; return; }
  if (state == 2) {
    int c = end.update(dt);
    if (c == 1) start(solo); else if (c == 2) wantExit = true;
    return;
  }
  left -= dt;
  float k = 1 - left / ROUND;
  int np = solo ? 1 : 2;
  for (int p = 1; p <= np; p++) {
    float tx = 0, ty;
    if (stun[p] > 0) stun[p] -= dt;
    else if (solo) soloTilt(x[p], BYP, tx, ty);
    else playerTilt(p, x[p], BYP, tx, ty, p == 1 ? 3 : 4);
    vx[p] += (tx * SPEED - vx[p]) * fminf(1.0f, 10 * dt);
    x[p] = constrain(x[p] + vx[p] * dt, 4 + BWID / 2, FW - 4 - BWID / 2);
  }
  if (!solo) {
    float gap = x[2] - x[1];
    if (fabsf(gap) < BWID) {
      float push = (BWID - fabsf(gap)) / 2 * (gap >= 0 ? 1 : -1);
      x[1] -= push; x[2] += push;
      float v1 = vx[1];
      vx[1] = vx[2] * 0.6f; vx[2] = v1 * 0.6f;
      for (int p = 1; p <= 2; p++) x[p] = constrain(x[p], 4 + BWID / 2, FW - 4 - BWID / 2);
    }
  }
  spawnT -= dt;
  if (spawnT <= 0) {
    int r = esp_random() % 104, kk = r < 55 ? 0 : r < 80 ? 1 : r < 86 ? 2 : 3;
    if (ni < 40) it[ni++] = {frand(12, FW - 12), -10, frand(63, 92) + 103 * k, frand(0, TWO_PI), kk};
    spawnT = (0.62f - 0.38f * k) * (solo ? 1.0f : 0.8f) * frand(0.6f, 1.4f);
  }
  j = 0;
  for (int i = 0; i < ni; i++) {
    Item &o = it[i];
    o.y += o.vy * dt;
    o.spin += dt * 3;
    bool caught = false;
    for (int p = 1; p <= np && !caught; p++) {
      if (fabsf(o.x - x[p]) < BWID / 2 + KINDS[o.k].r * 0.4f && o.y > BYP - 4 && o.y < BYP + 8) {
        int v = KINDS[o.k].val;
        score[p] = max(0, score[p] + v);
        if (o.k == 3) stun[p] = 1.0f;
        if (npop < 10) pops[npop++] = {o.x, BYP - 12, 0.8f, v};
        caught = true;
      }
    }
    if (!caught && o.y < FH + 10) it[j++] = o;
  }
  ni = j;
  if (left <= 0) {
    left = 0;
    state = 2;
    if (solo) isNew = hsBetter(HS_CATCH, score[1], false);
    else winner = score[1] > score[2] ? 1 : score[2] > score[1] ? 2 : 0;
  }
}
void frame() {
  float dt = frameDt();
  if (state == -1) {
    int c = pick.update(dt, 2);
    if (c >= 0) start(c == 0);
    else { pick.draw("Fruit Catch", MODES, MDESC, 2); return; }
  }
  step(dt);
  const uint16_t sky = rgb(40, 64, 96);
  g->fillRect(0, FY, FW, FH, sky);
  g->fillRect(0, SY(FH - 12), FW, 12, rgb(60, 120, 70));
  for (int i = 0; i < ni; i++) drawItem(it[i].x, it[i].y, it[i].k, it[i].spin);
  for (int p = 1; p <= (solo ? 1 : 2); p++) {
    uint16_t col = pcol(p);
    if (stun[p] > 0 && (int)(t * 12) % 2) col = rgb(120, 120, 130);
    int x0 = SX(x[p] - BWID / 2), x1 = SX(x[p] + BWID / 2), y0 = SY(BYP), y1 = SY(BYP + 11);
    g->fillTriangle(x0, y0, x1, y0, x1 - 5, y1, col);
    g->fillTriangle(x0, y0, x1 - 5, y1, x0 + 5, y1, col);
    g->drawFastHLine(x0, y0, x1 - x0, lighten(col, 0.3f));
    for (int i = 1; i < 4; i++) g->drawFastVLine(x0 + i * (x1 - x0) / 4, y0 + 1, 9, mix(col, 0, 0.5f));
  }
  for (int i = 0; i < npop; i++) {
    char b[6];
    snprintf(b, sizeof(b), "%+d", pops[i].val);
    text(b, SX(pops[i].x), SY(pops[i].y), &fonts::Font0, pops[i].val < 0 ? rgb(255, 120, 90) : GOLD, textdatum_t::middle_center);
  }
  char l[20], m[12];
  snprintf(m, sizeof(m), "%d s", (int)ceilf(left));
  if (solo) { snprintf(l, sizeof(l), "SCORE %d", score[1]); hudSolo(l, m, hs[HS_CATCH]); }
  else hud2(score[1], score[2], m);
  if (state == 0) countdown(timer, "catch fruit, not bombs!");
  else if (state == 2) {
    if (solo) { snprintf(l, sizeof(l), "%d points", score[1]); soloResult(l, isNew, hs[HS_CATCH], end); }
    else { snprintf(l, sizeof(l), "%d - %d", score[1], score[2]); matchResult(winner, l, end); }
  }
}
void tap(int px, int py) {
  if (state == -1) { int c = pick.tap(px, py, 2); if (c >= 0) start(c == 0); }
  else if (state == 2) start(solo);
}
}  // namespace fcatch

// =============================================================== Highway (1 player)
namespace hwy {
using namespace tg;
const float ROAD_L = 30, ROAD_R = 210, LANE_W = (ROAD_R - ROAD_L) / 4, CW = 16, CH = 28, PYP = FH - 56;
const uint16_t CARC[6] = {rgb(230, 230, 235), rgb(240, 200, 60), rgb(90, 200, 120), rgb(170, 110, 230), rgb(240, 140, 60), rgb(90, 200, 220)};
struct Car { float x, y, sp; uint16_t col; } car[16];
struct Coin { float x, y; } coin[8];
int ncar, ncoin, state, bonus;
float x, vx, speed, dist, scroll, spawnT, coinT, timer;
bool isNew;
Sparks fx;
EndChoice end;
float laneX(float i) { return ROAD_L + LANE_W * (i + 0.5f); }
int score() { return (int)(dist / 20) + bonus; }
void reset() {
  x = laneX(1.5f); vx = 0; speed = 120; dist = 0; bonus = 0; ncar = ncoin = 0; scroll = 0; spawnT = 1; coinT = 2;
  state = 0; timer = 2.5f; fx.clear(); end.reset();
}
void enter() { reset(); lastT = 0; }
void spawnCar() {
  int lane = esp_random() % 4;
  bool busy[4] = {false, false, false, false};
  int nbusy = 0;
  for (int i = 0; i < ncar; i++)
    if (car[i].y < 80) {
      int l = (int)lroundf((car[i].x - ROAD_L) / LANE_W - 0.5f);
      if (l >= 0 && l < 4 && !busy[l]) { busy[l] = true; nbusy++; }
    }
  if (busy[lane] || nbusy + 1 > 2 || ncar >= 16) return;  // always leave 2 lanes open
  car[ncar++] = {laneX(lane), -CH, frand(40, 80), CARC[esp_random() % 6]};
}
void step(float dt) {
  fx.step(dt);
  if (state == 0) { timer -= dt; if (timer <= 0) state = 1; return; }
  if (state == 2) {
    int c = end.update(dt);
    if (c == 1) reset(); else if (c == 2) wantExit = true;
    return;
  }
  float tx, ty;
  soloTilt(x, PYP, tx, ty);
  float base = 120 + fminf(110.0f, dist * 0.004f), target = base * (1 - ty * 0.55f);
  speed += (target - speed) * fminf(1.0f, 2.5f * dt);
  vx += (tx * 200 - vx) * fminf(1.0f, 9 * dt);
  x = constrain(x + vx * dt, ROAD_L + CW / 2 + 2, ROAD_R - CW / 2 - 2);
  dist += speed * dt;
  scroll = fmodf(scroll + speed * dt, 40);
  spawnT -= dt;
  if (spawnT <= 0) { spawnCar(); spawnT = fmaxf(0.35f, 1.3f - dist * 0.00004f) * frand(0.6f, 1.3f) * 120 / speed; }
  coinT -= dt;
  if (coinT <= 0) { if (ncoin < 8) coin[ncoin++] = {laneX(esp_random() % 4), -10}; coinT = frand(1.5f, 3.5f); }
  for (int i = 0; i < ncar; i++) car[i].y += (speed - car[i].sp) * dt;
  for (int i = 0; i < ncar; i++)  // a car that catches up slows to match the one in front
    for (int k = 0; k < ncar; k++)
      if (i != k && car[i].x == car[k].x && car[k].y > car[i].y && car[k].y - car[i].y < CH + 6) {
        car[i].y = car[k].y - CH - 6;
        car[i].sp = car[k].sp;
      }
  int j = 0;
  for (int i = 0; i < ncar; i++) if (car[i].y < FH + 40) car[j++] = car[i];
  ncar = j;
  j = 0;
  for (int i = 0; i < ncoin; i++) {
    coin[i].y += speed * dt;
    if (fabsf(coin[i].x - x) < CW / 2 + 4 && fabsf(coin[i].y - PYP) < CH / 2 + 4) {
      bonus += 20;
      for (int s = 0; s < 8; s++) { float a = frand(0, TWO_PI); fx.add(coin[i].x, coin[i].y, cosf(a) * 50, sinf(a) * 50, 0.4f, GOLD); }
    } else if (coin[i].y < FH + 10) {
      coin[j++] = coin[i];
    }
  }
  ncoin = j;
  for (int i = 0; i < ncar; i++)
    if (fabsf(car[i].x - x) < CW - 3 && fabsf(car[i].y - PYP) < CH - 4) {
      state = 2;
      isNew = hsBetter(HS_HIGHWAY, score(), false);
      for (int s = 0; s < 35; s++) {
        float a = frand(0, TWO_PI), v = frand(30, 140);
        fx.add(x, PYP, cosf(a) * v, sinf(a) * v, frand(0.4f, 1.0f), s % 2 ? rgb(255, 170, 60) : rgb(255, 90, 50));
      }
      break;
    }
}
void drawCar(float cx, float cy, uint16_t col, bool lights) {
  int x0 = SX(cx - CW / 2), y0 = SY(cy - CH / 2);
  g->fillRoundRect(x0 + 2, y0 + 2, CW, CH, 4, rgb(10, 10, 14));
  g->fillRoundRect(x0, y0, CW, CH, 4, col);
  g->fillRoundRect(x0 + 2, y0 + 6, CW - 5, 6, 2, rgb(40, 50, 70));
  g->fillRoundRect(x0 + 2, y0 + CH - 9, CW - 5, 4, 2, rgb(40, 50, 70));
  if (lights) { g->fillCircle(SX(cx - 5), y0 + 1, 2, rgb(255, 250, 200)); g->fillCircle(SX(cx + 5), y0 + 1, 2, rgb(255, 250, 200)); }
}
void frame() {
  step(frameDt());
  g->fillRect(0, FY, FW, FH, rgb(50, 110, 60));
  g->fillRect(SX(ROAD_L), FY, (int)(ROAD_R - ROAD_L), FH, rgb(52, 54, 62));
  g->fillRect(SX(ROAD_L), FY, 3, FH, rgb(230, 230, 230));
  g->fillRect(SX(ROAD_R - 3), FY, 3, FH, rgb(230, 230, 230));
  for (int i = 1; i < 4; i++)
    for (float y = -40 + scroll; y < FH; y += 40) {
      int y0 = max(FY, SY(y)), y1 = min(H, SY(y + 18));
      if (y1 > y0) g->fillRect(SX(ROAD_L + i * LANE_W - 1), y0, 2, y1 - y0, rgb(210, 210, 200));
    }
  for (float y = -60 + fmodf(scroll * 1.5f, 60); y < FH + 20; y += 60) {
    if (SY(y) >= FY) { g->fillCircle(14, SY(y), 9, rgb(30, 80, 40)); g->fillCircle(226, SY(y), 9, rgb(30, 80, 40)); }
  }
  for (int i = 0; i < ncoin; i++) { g->fillCircle(SX(coin[i].x), SY(coin[i].y), 5, GOLD); g->drawCircle(SX(coin[i].x), SY(coin[i].y), 3, rgb(200, 150, 30)); }
  for (int i = 0; i < ncar; i++) drawCar(car[i].x, car[i].y, car[i].col, false);
  if (state != 2) drawCar(x, PYP, BLUE, true);
  fx.draw(2);
  g->fillRect(0, APP_Y, W, FY - APP_Y, C_CARD);  // cars entering from the top are clipped by the HUD
  char l[20], m[16];
  snprintf(l, sizeof(l), "SCORE %d", score());
  snprintf(m, sizeof(m), "%.0f km/h", speed * 0.6f);
  hudSolo(l, m, hs[HS_HIGHWAY]);
  if (state == 0) countdown(timer, "steer; tilt up = faster");
  else if (state == 2) { snprintf(l, sizeof(l), "%d points", score()); soloResult(l, isNew, hs[HS_HIGHWAY], end); }
}
void tap(int, int) { if (state == 2) reset(); }
}  // namespace hwy

// =============================================================== Star Shooter (solo or co-op)
namespace shoot {
using namespace tg;
const float SHIP_R = 6, FIRE = 0.2f, TOPL = FH * 0.58f, BOTL = FH - 10;
const int MAXP = 5;
struct Ship { int p, power; float x, y, vx, vy, fire, lost, inv, dead; } ships[2];
struct Enemy { float x, y, hx, hy, r, ph, fire; int hp, mx, pts; uint16_t col; bool boss; } en[40];
struct Shot { float x, y; int p; } shots[60];
struct Bomb { float x, y, vx, vy; } bombs[50];
struct Cap { float x, y; bool gun; } caps[6];
int nship, nen, nshot, nbomb, ncap, lives, score, wave, state = -1;  // -1 pick, 0 countdown, 1 play, 2 over
float t, timer, banner;
bool solo = true, isNew;
Sparks fx;
EndChoice end;
Picker pick;
const char *MODES[] = {"Solo", "Co-op (2 ships)"};
const char *MDESC[] = {"3 lives, best score is saved", "Shared lives and score"};
void start(bool s) {
  solo = s;
  nship = solo ? 1 : 2;
  for (int i = 0; i < nship; i++) ships[i] = {i + 1, 1, solo ? 120.0f : (i ? 160.0f : 80.0f), FH - 35, 0, 0, frand(0, FIRE), 0, 2, 0};
  lives = solo ? 3 : 5; score = 0; wave = 0; nen = nshot = nbomb = ncap = 0; t = 0; state = 0; timer = 2; banner = 0;
  fx.clear(); end.reset();
}
void enter() { state = -1; pick.reset(); lastT = 0; }
void enterCoop() { start(false); lastT = 0; }  // the Co-op tile skips the mode picker
void nextWave() {
  wave++;
  banner = 1.6f;
  if (wave % 5 == 0) {
    int hp = 30 + 10 * wave;
    en[0] = {120, -30, 120, 50, 18, 0, 1, hp, hp, 200 + 20 * wave, rgb(230, 90, 160), true};
    nen = 1;
    return;
  }
  const uint16_t SC[3] = {rgb(240, 90, 90), rgb(240, 180, 60), rgb(160, 110, 240)};
  const int SHP[3] = {1, 2, 3}, SPT[3] = {10, 20, 40};
  int rows = min(4, 2 + wave / 3), cols = min(6, 4 + wave / 2);
  nen = 0;
  for (int r = 0; r < rows; r++) {
    int k = min(2, (rows - 1 - r) * (1 + wave / 4) / 3);
    for (int c = 0; c < cols && nen < 40; c++) {
      float hx = 120 + (c - (cols - 1) / 2.0f) * 30, hy = 26 + r * 22;
      en[nen++] = {hx, hy - 140 - c * 6, hx, hy, 7, frand(0, TWO_PI), frand(1.5f, 6), SHP[k], SHP[k], SPT[k], SC[k], false};
    }
  }
}
void hitShip(Ship &s) {
  if (s.inv > 0 || s.dead > 0) return;
  fx.burst(s.x, s.y, pcol(s.p), 30, 140);
  lives--;
  s.dead = 1.5f;
  if (s.power > 1) s.lost = 1.5f;
  s.power = 1;
  if (lives <= 0) {
    state = 2;
    isNew = hsBetter(solo ? HS_SHOOTER : HS_SHOOTER_COOP, score, false);
  }
}
void step(float dt) {
  t += dt;
  fx.step(dt);
  banner = fmaxf(0.0f, banner - dt);
  if (state == 0) { timer -= dt; if (timer <= 0) { state = 1; nextWave(); } return; }
  if (state == 2) {
    int c = end.update(dt);
    if (c == 1) start(solo); else if (c == 2) wantExit = true;
    return;
  }
  for (int i = 0; i < nship; i++) {
    Ship &s = ships[i];
    s.lost = fmaxf(0.0f, s.lost - dt);
    if (s.dead > 0) {
      s.dead -= dt;
      if (s.dead <= 0 && lives > 0) { s.x = 120; s.y = FH - 35; s.vx = s.vy = 0; s.inv = 2; }
      continue;
    }
    s.inv = fmaxf(0.0f, s.inv - dt);
    float tx, ty;
    if (solo) soloTilt(s.x, s.y, tx, ty);
    else playerTilt(s.p, s.x, s.y, tx, ty, s.p == 1 ? 3 : 4);
    s.vx += (tx * 210 - s.vx) * fminf(1.0f, 10 * dt);
    s.vy += (ty * 200 - s.vy) * fminf(1.0f, 10 * dt);
    s.x = constrain(s.x + s.vx * dt, 10.0f, FW - 10.0f);
    s.y = constrain(s.y + s.vy * dt, TOPL, BOTL);
    s.fire -= dt;
    if (s.fire <= 0) {
      s.fire = FIRE;
      for (int k = 0; k < s.power && nshot < 60; k++) {
        float off = k - (s.power - 1) / 2.0f;
        shots[nshot++] = {s.x + off * 4.5f, s.y - 7 + fabsf(off), s.p};
      }
    }
  }
  int j = 0;
  for (int i = 0; i < nshot; i++) { shots[i].y -= 330 * dt; if (shots[i].y > -10) shots[j++] = shots[i]; }
  nshot = j;
  float speedK = 1 + 0.08f * wave;
  int alive[2], na = 0;
  for (int i = 0; i < nship; i++) if (ships[i].dead <= 0) alive[na++] = i;
  for (int i = 0; i < nen; i++) {
    Enemy &e = en[i];
    e.ph += dt * (e.boss ? 0.8f : 1.6f);
    float tx = e.hx + sinf(e.ph) * (e.boss ? 50 : 12);
    float ty = e.hy + (e.boss ? sinf(e.ph * 0.7f) * 12 : sinf(e.ph * 2) * 3);
    e.x += (tx - e.x) * fminf(1.0f, 3 * dt);
    e.y += (ty - e.y) * fminf(1.0f, 2 * dt);
    e.fire -= dt * speedK;
    if (e.fire <= 0 && na && e.y > 0) {
      if (e.boss) {
        for (int a = -2; a <= 2 && nbomb < 50; a++) bombs[nbomb++] = {e.x, e.y + 14, sinf(a * 0.25f) * 110, cosf(a * 0.25f) * 110};
        e.fire = 1.4f;
      } else {
        Ship &tg2 = ships[alive[esp_random() % na]];
        float dx = tg2.x - e.x, dy = tg2.y - e.y, d = fmaxf(1.0f, hypotf(dx, dy)), v = 95 + 7 * wave;
        if (nbomb < 50) bombs[nbomb++] = {e.x, e.y + 6, dx / d * v * 0.4f, v};
        e.fire = frand(3, 7);
      }
    }
  }
  j = 0;
  for (int i = 0; i < nbomb; i++) {
    bombs[i].x += bombs[i].vx * dt; bombs[i].y += bombs[i].vy * dt;
    if (bombs[i].x > -10 && bombs[i].x < FW + 10 && bombs[i].y < FH + 10) bombs[j++] = bombs[i];
  }
  nbomb = j;
  j = 0;
  for (int i = 0; i < ncap; i++) { caps[i].y += 55 * dt; if (caps[i].y < FH + 10) caps[j++] = caps[i]; }
  ncap = j;
  // shots vs enemies
  j = 0;
  for (int i = 0; i < nshot; i++) {
    bool used = false;
    for (int k = 0; k < nen && !used; k++) {
      Enemy &e = en[k];
      if (e.hp > 0 && fabsf(shots[i].x - e.x) < e.r && fabsf(shots[i].y - e.y) < e.r) {
        used = true;
        if (--e.hp <= 0) {
          score += e.pts;
          fx.burst(e.x, e.y, e.col, e.boss ? 40 : 14);
          float r = frand(0, 1);
          if ((e.boss || r < 0.05f) && ncap < 6) caps[ncap++] = {e.x, e.y, true};
          else if (r < 0.065f && ncap < 6) caps[ncap++] = {e.x, e.y, false};
        }
      }
    }
    if (!used) shots[j++] = shots[i];
  }
  nshot = j;
  j = 0;
  for (int i = 0; i < nen; i++) if (en[i].hp > 0) en[j++] = en[i];
  nen = j;
  for (int i = 0; i < nship; i++) {
    Ship &s = ships[i];
    if (s.dead > 0) continue;
    for (int k = 0; k < nbomb; k++)
      if (hypotf(bombs[k].x - s.x, bombs[k].y - s.y) < SHIP_R) {
        bombs[k] = bombs[--nbomb];
        hitShip(s);
        break;
      }
    for (int k = 0; k < nen; k++)
      if (hypotf(en[k].x - s.x, en[k].y - s.y) < en[k].r + SHIP_R - 2) hitShip(s);
    for (int k = 0; k < ncap; k++)
      if (hypotf(caps[k].x - s.x, caps[k].y - s.y) < SHIP_R + 6) {
        if (caps[k].gun) { for (int m2 = 0; m2 < nship; m2++) ships[m2].power = min(MAXP, ships[m2].power + 1); fx.burst(caps[k].x, caps[k].y, rgb(90, 200, 240), 12, 60); }
        else lives++;
        caps[k--] = caps[--ncap];
      }
  }
  if (state == 1 && !nen) { score += 50 * wave; nbomb = 0; nextWave(); }
}
void drawShip(const Ship &s) {
  if (s.inv > 0 && (int)(t * 12) % 2) return;
  int x = SX(s.x), y = SY(s.y);
  uint16_t col = pcol(s.p);
  if ((int)(t * 20) % 2) g->fillCircle(x, y + 6, 2, rgb(255, 170, 60));
  g->fillTriangle(x, y - 8, x + 7, y + 6, x, y + 3, col);
  g->fillTriangle(x, y - 8, x - 7, y + 6, x, y + 3, col);
  g->fillCircle(x, y - 1, 2, rgb(220, 240, 255));
  for (int i = 0; i < s.power - 1; i++) g->drawCircle(x, y, 10 + i * 2.5f, rgb(90, 200, 240));
}
void drawEnemy(const Enemy &e) {
  int x = SX(e.x), y = SY(e.y), r = (int)e.r;
  if (e.boss) {
    g->fillEllipse(x, y, r * 1.4f, r * 0.7f, e.col);
    g->fillCircle(x, y - r * 0.4f, r * 0.6f, rgb(250, 200, 230));
    for (int i = 0; i < 5; i++) g->fillCircle(x - r + i * r / 2, y + r * 0.2f, 2, ((int)(t * 6) + i) % 2 ? rgb(255, 240, 120) : rgb(120, 60, 90));
    int w = 60;
    g->fillRect(x - w / 2, y - r - 8, w, 3, rgb(60, 30, 40));
    g->fillRect(x - w / 2, y - r - 8, w * e.hp / e.mx, 3, rgb(240, 90, 120));
    return;
  }
  float wob = sinf(t * 8 + e.ph) * 1.5f;
  g->fillCircle(x, y, r, e.col);
  g->fillRect(x - r, y, 2 * r, (int)(r * 0.7f), e.col);
  for (int k = -1; k <= 1; k++) g->drawLine(x + k * r * 0.7f, y + r * 0.6f, x + k * r * 0.7f + wob, y + r + 2, e.col);
  g->fillCircle(x - 2, y - 1, 1, rgb(20, 20, 30));
  g->fillCircle(x + 2, y - 1, 1, rgb(20, 20, 30));
  if (e.hp < e.mx) g->drawCircle(x, y, r, C_TEXT);
}
void frame() {
  float dt = frameDt();
  if (state == -1) {
    int c = pick.update(dt, 2);
    if (c >= 0) start(c == 0);
    else { pick.draw("Star Shooter", MODES, MDESC, 2); return; }
  }
  step(dt);
  g->fillRect(0, FY, FW, FH, rgb(8, 10, 22));
  uint32_t seed = 3;
  for (int i = 0; i < 50; i++) {  // drifting star field (fixed pseudo-random positions)
    seed = seed * 1103515245 + 12345;
    float sx = (seed >> 8) % FW;
    seed = seed * 1103515245 + 12345;
    float sy = fmodf((seed >> 8) % FH + t * (10 + i % 3 * 12), FH);
    g->drawPixel(SX(sx), SY(sy), i % 3 ? rgb(90, 95, 130) : rgb(160, 165, 200));
  }
  for (int i = 0; i < nshot; i++) g->fillRect(SX(shots[i].x) - 1, SY(shots[i].y) - 4, 2, 7, lighten(pcol(shots[i].p), 0.35f));
  for (int i = 0; i < nen; i++) drawEnemy(en[i]);
  for (int i = 0; i < nbomb; i++) { g->fillCircle(SX(bombs[i].x), SY(bombs[i].y), 3, rgb(255, 120, 90)); g->fillCircle(SX(bombs[i].x), SY(bombs[i].y), 1, rgb(255, 230, 180)); }
  for (int i = 0; i < ncap; i++) {
    g->fillRoundRect(SX(caps[i].x) - 8, SY(caps[i].y) - 5, 16, 10, 5, caps[i].gun ? rgb(80, 200, 240) : rgb(240, 90, 160));
    text(caps[i].gun ? "+1" : "+", SX(caps[i].x), SY(caps[i].y), &fonts::Font0, rgb(20, 20, 30), textdatum_t::middle_center);
  }
  if (state != 2) for (int i = 0; i < nship; i++) if (ships[i].dead <= 0) drawShip(ships[i]);
  fx.draw(1);
  g->fillRect(0, APP_Y, W, FY - APP_Y, C_CARD);
  char l[20], m[24];
  snprintf(l, sizeof(l), "SCORE %d", score);
  snprintf(m, sizeof(m), "wave %d  lives %d", wave, lives);
  hudSolo(l, m, hs[solo ? HS_SHOOTER : HS_SHOOTER_COOP]);
  if (state == 1) {
    for (int i = 0; i < nship; i++) {
      const Ship &s = ships[i];
      int x = nship == 1 ? 120 : (i ? 180 : 60);
      if (s.lost > 0) text("POWER LOST!", x, H - 10, &fonts::Font0, rgb(240, 100, 100), textdatum_t::middle_center);
      else if (s.power > 1) {
        snprintf(m, sizeof(m), s.power >= MAXP ? "%dx MAX" : "%dx", s.power);
        text(m, x, H - 10, &fonts::Font2, solo ? rgb(90, 200, 240) : pcol(s.p), textdatum_t::middle_center);
      }
    }
    if (banner > 0) {
      bool boss = wave % 5 == 0;
      snprintf(m, sizeof(m), "WAVE %d", wave);
      text(boss ? "BOSS WAVE!" : m, 120, 180, &fonts::FreeSansBold12pt7b, boss ? rgb(240, 90, 160) : C_TEXT, textdatum_t::middle_center);
    }
  }
  if (state == 0) countdown(timer, solo ? "you fire by yourself - tilt to fly" : "team up! shared lives");
  else if (state == 2) { snprintf(l, sizeof(l), "%d points", score); soloResult(l, isNew, hs[solo ? HS_SHOOTER : HS_SHOOTER_COOP], end); }
}
void tap(int px, int py) {
  if (state == -1) { int c = pick.tap(px, py, 2); if (c >= 0) start(c == 0); }
  else if (state == 2) start(solo);
}
}  // namespace shoot

// =============================================================== Cave Flyer (1 player or 2-player VS), portrait
// The cave scrolls down the screen; the ship flies up. Tilt left/right to steer.
namespace cave {
using namespace tg;
const float ROW = 4, SHIP_Y = FH - 60, SHIP_R = 5;
const int NROWS = 96, WIN = 3;
const char *MODES[] = {"1 player", "2 players (VS)"};
const char *MDESC[] = {"Fly as far as you can, grab gems", "Bump them into the rock. First to 3"};
struct Ship { int p; float x, vx; bool alive; } ships[2];
struct Gem { float x, y; } gems[8];
float rl[NROWS], rr[NROWS];  // left/right wall of each cave row (circular, newest at head)
int head, nrows, nship, ngem, gemPts, state = -1, score2[3], roundWinner, winner;  // -1 pick, 0 countdown, 1 play, 2 round over, 3 over
float newestY, dist, speed, center, cdrift, timer, t;
bool solo = true, isNew;
Sparks fx;
EndChoice end;
Picker pick;
int score() { return (int)(dist / 10) + gemPts; }
float gap() { return solo ? fmaxf(62.0f, 150.0f - dist * 0.012f) : fmaxf(72.0f, 160.0f - dist * 0.012f); }
void addRow(bool warmup) {
  float gp = gap();
  if (!warmup) {
    cdrift = constrain(cdrift + frand(-0.45f, 0.45f), -2.2f, 2.2f) * 0.985f;
    center += cdrift;
  }
  float lo = gp / 2 + 6, hi = FW - gp / 2 - 6;
  if (center < lo || center > hi) { center = constrain(center, lo, hi); cdrift = -cdrift * 0.5f; }
  float wob = sinf(dist * 0.05f + nrows) * 3;
  head = (head + 1) % NROWS;
  rl[head] = center - gp / 2 + wob;
  rr[head] = center + gp / 2 + wob;
  if (nrows < NROWS) nrows++;
  if (solo && !warmup && frand(0, 1) < 0.02f && ngem < 8) gems[ngem++] = {center + frand(-gp / 4, gp / 4), newestY};
}
void wallsAt(float y, float &l, float &r) {
  int k = (int)floorf((y - newestY) / ROW);
  if (k < 0 || k >= nrows) { l = 0; r = FW; return; }
  int i = (head - k + NROWS) % NROWS;
  l = rl[i]; r = rr[i];
}
void startRound() {
  dist = 0; speed = 70; gemPts = 0; ngem = 0; center = FW / 2; cdrift = 0; head = 0; nrows = 0;
  nship = solo ? 1 : 2;
  ships[0] = {1, solo ? 120.0f : 105.0f, 0, true};
  ships[1] = {2, 135, 0, true};
  newestY = FH;
  while (newestY > -ROW) { newestY -= ROW; addRow(true); }
  state = 0; timer = 2.5f; roundWinner = 0;
  fx.clear();
}
void start(bool s) {
  solo = s;
  score2[1] = score2[2] = 0; winner = 0; t = 0;
  end.reset();
  startRound();
}
void enter() { state = -1; pick.reset(); lastT = 0; }
bool crashed(const Ship &s) {
  for (float y : {SHIP_Y - SHIP_R, SHIP_Y, SHIP_Y + SHIP_R}) {
    float l, r;
    wallsAt(y, l, r);
    if (s.x - SHIP_R * 0.8f < l || s.x + SHIP_R * 0.8f > r) return true;
  }
  return false;
}
void step(float dt) {
  t += dt;
  fx.step(dt);
  if (state == 0) { timer -= dt; if (timer <= 0) state = 1; return; }
  if (state == 2) {
    timer -= dt;
    if (timer <= 0) {
      if (max(score2[1], score2[2]) >= WIN) { winner = score2[1] > score2[2] ? 1 : 2; state = 3; }
      else startRound();
    }
    return;
  }
  if (state == 3) {
    int c = end.update(dt);
    if (c == 1) start(solo); else if (c == 2) wantExit = true;
    return;
  }
  for (int i = 0; i < nship; i++) {
    Ship &s = ships[i];
    if (!s.alive) continue;
    float tx, ty;
    if (solo) soloTilt(s.x, SHIP_Y, tx, ty);
    else playerTilt(s.p, s.x, SHIP_Y, tx, ty, s.p == 1 ? 3 : 4);
    s.vx += (tx * 300 - s.vx) * fminf(1.0f, 6 * dt);
    s.x += s.vx * dt;
  }
  if (!solo && ships[0].alive && ships[1].alive) {  // ships bump each other sideways
    Ship *a = &ships[0], *b = &ships[1];
    if (a->x > b->x) { Ship *tmp = a; a = b; b = tmp; }
    float ov = 2 * SHIP_R + 1 - (b->x - a->x);
    if (ov > 0) {
      a->x -= ov / 2; b->x += ov / 2;
      if (a->vx - b->vx > 0) {
        float av = a->vx;
        a->vx = b->vx - 40; b->vx = av + 40;
        for (int k = 0; k < 4; k++) { float an = frand(0, TWO_PI), v = frand(20, 60); fx.add((a->x + b->x) / 2, SHIP_Y, cosf(an) * v, sinf(an) * v, 0.25f, C_TEXT); }
      }
    }
  }
  speed = fminf(170.0f, 70.0f + dist * 0.01f);
  float dy = speed * dt;
  dist += dy;
  newestY += dy;
  for (int i = 0; i < ngem; i++) gems[i].y += dy;
  while (newestY > -ROW) { newestY -= ROW; addRow(false); }
  int j = 0;
  for (int i = 0; i < ngem; i++) {
    if (hypotf(gems[i].x - ships[0].x, gems[i].y - SHIP_Y) < SHIP_R + 5) {
      gemPts += 25;
      for (int k = 0; k < 10; k++) { float an = frand(0, TWO_PI); fx.add(gems[i].x, gems[i].y, cosf(an) * 60, sinf(an) * 60, 0.4f, rgb(120, 220, 255)); }
    } else if (gems[i].y < FH + 10) {
      gems[j++] = gems[i];
    }
  }
  ngem = j;
  for (int i = 0; i < nship; i++)
    if (ships[i].alive && crashed(ships[i])) {
      ships[i].alive = false;
      for (int k = 0; k < 30; k++) { float an = frand(0, TWO_PI), v = frand(30, 120); fx.add(ships[i].x, SHIP_Y, cosf(an) * v, sinf(an) * v, 0.8f, rgb(255, 170, 60)); }
    }
  if (solo) {
    if (!ships[0].alive) { state = 3; isNew = hsBetter(HS_CAVE, score(), false); }
  } else if (!ships[0].alive || !ships[1].alive) {
    roundWinner = ships[0].alive ? 1 : ships[1].alive ? 2 : 0;
    if (roundWinner) score2[roundWinner]++;
    state = 2;
    timer = 2.2f;
  }
}
void drawShip(const Ship &s) {
  float a = constrain(s.vx / 400, -0.5f, 0.5f), ca = cosf(a), sa = sinf(a);
  const float P[4][2] = {{0, -9}, {-5, 6}, {0, 3}, {5, 6}};
  int px[4], py[4];
  for (int k = 0; k < 4; k++) { px[k] = SX(s.x + P[k][0] * ca - P[k][1] * sa); py[k] = SY(SHIP_Y + P[k][0] * sa + P[k][1] * ca); }
  if ((int)(t * 20 + s.p) % 2) g->fillCircle(SX(s.x), SY(SHIP_Y + 8), 2, rgb(255, 160, 60));
  uint16_t col = pcol(s.p);
  g->fillTriangle(px[0], py[0], px[1], py[1], px[2], py[2], col);
  g->fillTriangle(px[0], py[0], px[2], py[2], px[3], py[3], col);
  g->drawLine(px[0], py[0], px[1], py[1], C_TEXT);
  g->drawLine(px[0], py[0], px[3], py[3], C_TEXT);
}
void frame() {
  float dt = frameDt();
  if (state == -1) {
    int c = pick.update(dt, 2);
    if (c >= 0) start(c == 0);
    else { pick.draw("Cave Flyer", MODES, MDESC, 2); return; }
  }
  step(dt);
  const uint16_t rock = rgb(70, 62, 80), edge = rgb(130, 115, 150);
  g->fillRect(0, FY, FW, FH, rgb(12, 14, 24));
  for (int k = 0; k < nrows; k++) {
    int i = (head - k + NROWS) % NROWS, y = SY(newestY + k * ROW);
    if (y >= H) break;
    int l = (int)rl[i], r = (int)rr[i];
    g->fillRect(0, y, l, (int)ROW, rock);
    g->fillRect(r, y, FW - r, (int)ROW, rock);
    g->fillRect(l - 2, y, 2, (int)ROW, edge);
    g->fillRect(r, y, 2, (int)ROW, edge);
  }
  for (int i = 0; i < ngem; i++) {
    int x = SX(gems[i].x), y = SY(gems[i].y);
    g->fillTriangle(x, y - 5, x + 4, y, x, y + 5, rgb(120, 220, 255));
    g->fillTriangle(x, y - 5, x - 4, y, x, y + 5, rgb(120, 220, 255));
  }
  fx.draw(1);
  for (int i = 0; i < nship; i++) if (ships[i].alive) drawShip(ships[i]);
  g->fillRect(0, APP_Y, W, FY - APP_Y, C_CARD);  // rows entering at the top are clipped by the HUD
  char l[24], m[24];
  if (solo) {
    snprintf(l, sizeof(l), "SCORE %d", score());
    snprintf(m, sizeof(m), "%.0f m", dist / 100);
    hudSolo(l, m, hs[HS_CAVE]);
  } else {
    snprintf(m, sizeof(m), "%.0f m - first to %d", dist / 100, WIN);
    hud2(score2[1], score2[2], m);
  }
  if (state == 0) countdown(timer, solo ? "tilt left / right to steer" : "bump them into the rock!");
  else if (state == 2) {
    panel(140, 50);
    snprintf(m, sizeof(m), "%s SURVIVES!", roundWinner ? pname(roundWinner) : "");
    text(roundWinner ? m : "BOTH CRASHED!", 120, 165, &fonts::FreeSansBold9pt7b, roundWinner ? pcol(roundWinner) : C_TEXT, textdatum_t::middle_center);
  } else if (state == 3) {
    if (solo) { snprintf(l, sizeof(l), "%d points", score()); soloResult(l, isNew, hs[HS_CAVE], end); }
    else { snprintf(m, sizeof(m), "%d - %d", score2[1], score2[2]); matchResult(winner, m, end); }
  }
}
void tap(int px2, int py) {
  if (state == -1) { int c = pick.tap(px2, py, 2); if (c >= 0) start(c == 0); }
  else if (state == 3) start(solo);
}
}  // namespace cave

// =============================================================== Pong (vs the display, or 2 players)
namespace pong {
using namespace tg;
const float PADW = 46, PADH = 6, BALLR = 4, P1Y = FH - 14, P2Y = 8;
const int WIN = 7;
const char *MODES[] = {"vs Display", "2 players"};
const char *MDESC[] = {"Tilt left/right. First to 7", "BLUE bottom, RED top. First to 7"};
float px[3], pvx[3], bx, by, vx, vy, sp, timer, t;
int score[3], state = -1, winner, server;  // -1 pick, 0 serve countdown, 1 play, 2 match over
bool cpu = true;
EndChoice end;
Picker pick;
void serve() {
  bx = 120; by = FH / 2.0f; sp = 170;
  float a = frand(-0.5f, 0.5f);
  vx = sinf(a) * sp;
  vy = cosf(a) * sp * (server == 1 ? -1 : 1);
  state = 0; timer = 1.2f;
}
void start(bool vsCpu) {
  cpu = vsCpu;
  px[1] = px[2] = 120; pvx[1] = pvx[2] = 0;
  score[1] = score[2] = 0; winner = 0; t = 0; server = 1;
  end.reset();
  serve();
}
void enter() { state = -1; pick.reset(); lastT = 0; }
void movePaddle(int p, float dt) {
  float tx = 0, ty;
  if (p == 2 && cpu) {
    float target = vy < 0 ? bx + (bx - 120) * 0.1f : 120;  // follow the ball when it comes up
    float d = target - px[2];
    pvx[2] = constrain(d * 6, -165.0f, 165.0f);
  } else {
    playerTilt(p, px[p], p == 1 ? P1Y : P2Y, tx, ty, p == 1 ? 1 : 2);
    pvx[p] += (tx * 280 - pvx[p]) * fminf(1.0f, 10 * dt);
  }
  px[p] = constrain(px[p] + pvx[p] * dt, PADW / 2, FW - PADW / 2);
}
void bounce(int p) {
  float rel = constrain((bx - px[p]) / (PADW / 2), -1.0f, 1.0f);
  sp = fminf(sp * 1.06f, 420.0f);
  vx = sp * rel * 0.75f + pvx[p] * 0.15f;
  float vyAbs = sqrtf(fmaxf(sp * sp - vx * vx, sp * sp * 0.25f));
  vy = p == 1 ? -vyAbs : vyAbs;
}
void step(float dt) {
  t += dt;
  if (state == 2) {
    int c = end.update(dt);
    if (c == 1) start(cpu); else if (c == 2) wantExit = true;
    return;
  }
  movePaddle(1, dt);
  movePaddle(2, dt);
  if (state == 0) { timer -= dt; if (timer <= 0) state = 1; return; }
  for (int s = 0; s < 3; s++) {
    float h = dt / 3;
    bx += vx * h;
    by += vy * h;
    if (bx < BALLR) { bx = BALLR; vx = fabsf(vx); }
    if (bx > FW - BALLR) { bx = FW - BALLR; vx = -fabsf(vx); }
    if (vy > 0 && by + BALLR >= P1Y && by < P1Y + PADH && fabsf(bx - px[1]) < PADW / 2 + BALLR) bounce(1);
    if (vy < 0 && by - BALLR <= P2Y + PADH && by > P2Y && fabsf(bx - px[2]) < PADW / 2 + BALLR) bounce(2);
    if (by < -10 || by > FH + 10) {
      int sc = by < 0 ? 1 : 2;
      score[sc]++;
      server = sc == 1 ? 2 : 1;
      if (score[sc] >= WIN) { winner = sc; state = 2; }
      else serve();
      return;
    }
  }
}
void frame() {
  float dt = frameDt();
  if (state == -1) {
    int c = pick.update(dt, 2);
    if (c >= 0) start(c == 0);
    else { pick.draw("Pong", MODES, MDESC, 2); return; }
  }
  step(dt);
  g->fillRect(0, FY, FW, FH, FIELD);
  for (int x = 4; x < FW; x += 16) g->fillRect(x, SY(FH / 2) - 1, 8, 2, C_TRACK);
  char b[4];
  snprintf(b, sizeof(b), "%d", score[2]);
  text(b, 210, SY(FH / 2 - 26), &fonts::Font4, mix(RED, FIELD, 0.5f), textdatum_t::middle_center);
  snprintf(b, sizeof(b), "%d", score[1]);
  text(b, 210, SY(FH / 2 + 26), &fonts::Font4, mix(BLUE, FIELD, 0.5f), textdatum_t::middle_center);
  g->fillRoundRect(SX(px[1] - PADW / 2), SY(P1Y), (int)PADW, (int)PADH, 3, BLUE);
  g->fillRoundRect(SX(px[2] - PADW / 2), SY(P2Y), (int)PADW, (int)PADH, 3, RED);
  if (state != 2) g->fillCircle(SX(bx), SY(by), BALLR, C_TEXT);
  hud2(score[1], score[2], cpu ? "vs display" : "first to 7");
  if (state == 0 && score[1] + score[2] == 0) text(pads::any() ? "tilt left / right" : "drag to move your paddle", 120, SY(FH / 2 + 60), &fonts::Font2, C_DIM, textdatum_t::middle_center);
  if (state == 2) {
    char m[12];
    snprintf(m, sizeof(m), "%d - %d", score[1], score[2]);
    matchResult(winner, m, end);
  }
}
void tap(int px2, int py) {
  if (state == -1) { int c = pick.tap(px2, py, 2); if (c >= 0) start(c == 0); }
  else if (state == 2) start(cpu);
}
}  // namespace pong
