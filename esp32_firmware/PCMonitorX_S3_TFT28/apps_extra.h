// Calculator, Calendar, Simon, Slide puzzle and the Controllers page.
// Included from PCMonitorX_S3_TFT28.ino after tilt_games.h.
#pragma once

// =============================================================== Calculator
namespace calc {
char disp[24] = "0", hist[40] = "";
double acc = 0;
char op = 0;
bool fresh = true, err = false;
const char *KEYS[20] = {"C", "+/-", "%", "/", "7", "8", "9", "x", "4", "5", "6", "-", "1", "2", "3", "+", "0", ".", "DEL", "="};
const int KX = 4, KW = 55, KG = 4, KY = APP_Y + 82, KH = 39;
double val() { return atof(disp); }
void show(double v) {
  if (!isfinite(v)) { strcpy(disp, "Error"); err = true; return; }
  snprintf(disp, sizeof(disp), "%.10g", v);
}
double apply(double a, char o, double b) {
  switch (o) {
    case '+': return a + b;
    case '-': return a - b;
    case 'x': return a * b;
    case '/': return b == 0 ? NAN : a / b;
  }
  return b;
}
void press(int k) {
  const char *s = KEYS[k];
  if (err && k != 0) { strcpy(disp, "0"); err = false; fresh = true; }
  if (isdigit((unsigned char)s[0])) {
    if (fresh || !strcmp(disp, "0")) { disp[0] = 0; fresh = false; }
    if (strlen(disp) < 15) strcat(disp, s);
  } else if (!strcmp(s, ".")) {
    if (fresh) { strcpy(disp, "0"); fresh = false; }
    if (!strchr(disp, '.')) strcat(disp, ".");
  } else if (!strcmp(s, "C")) {
    strcpy(disp, "0"); acc = 0; op = 0; fresh = true; hist[0] = 0;
  } else if (!strcmp(s, "+/-")) {
    show(-val());
  } else if (!strcmp(s, "%")) {
    show(op ? acc * val() / 100 : val() / 100);
  } else if (!strcmp(s, "DEL")) {
    if (!fresh) {
      size_t L = strlen(disp);
      if (L > 1 && !(L == 2 && disp[0] == '-')) disp[L - 1] = 0;
      else strcpy(disp, "0");
    }
  } else if (!strcmp(s, "=")) {
    if (op) {
      double b = val();
      snprintf(hist, sizeof(hist), "%.8g %c %.8g =", acc, op, b);
      show(apply(acc, op, b));
      op = 0;
      fresh = true;
    }
  } else {  // + - x /
    if (op && !fresh) show(apply(acc, op, val()));
    acc = val();
    op = s[0];
    fresh = true;
    snprintf(hist, sizeof(hist), "%.10g %c", acc, op);
  }
}
void frame() {
  card(4, APP_Y + 4, 232, 72);
  text(hist, 228, APP_Y + 16, &fonts::Font2, C_DIM, textdatum_t::middle_right);
  g->setFont(&fonts::FreeSansBold18pt7b);
  const lgfx::IFont *f = g->textWidth(disp) <= 220 ? (const lgfx::IFont *)&fonts::FreeSansBold18pt7b : (const lgfx::IFont *)&fonts::Font4;
  text(disp, 228, APP_Y + 52, f, err ? C_BAD : C_TEXT, textdatum_t::middle_right);
  for (int k = 0; k < 20; k++) {
    int c = k % 4, r = k / 4, x = KX + c * (KW + KG), y = KY + r * (KH + KG);
    bool opKey = c == 3 && r < 4;
    uint16_t bg = k == 19 ? C_GOOD : opKey ? (op == KEYS[k][0] && fresh ? C_TEXT : APP_COLORS[A_CALC]) : r == 0 ? C_TRACK : C_CARD2;
    uint16_t fg = k == 19 || opKey ? C_BLACK : C_TEXT;
    g->fillRoundRect(x, y, KW, KH, 8, bg);
    text(KEYS[k], x + KW / 2, y + KH / 2, isdigit((unsigned char)KEYS[k][0]) ? &fonts::Font4 : &fonts::Font2, fg, textdatum_t::middle_center);
  }
}
void tap(int x, int y) {
  if (y < KY || x < KX) return;
  int c = (x - KX) / (KW + KG), r = (y - KY) / (KH + KG);
  if (c < 4 && r < 5) press(r * 4 + c);
}
}  // namespace calc

// =============================================================== Calendar
namespace cal {
int yr = 0, mon = 0;
const char *MONTH_FULL[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
int dow(int y, int m, int d) {  // 0 = Sunday
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
int daysIn(int y, int m) {
  static const int D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0) ? 29 : D[m - 1];
}
void today() {
  if (haveClock()) { Civil c = civil(nowLocalMs()); yr = c.y; mon = c.mo; }
  else if (!yr) { yr = 2026; mon = 1; }
}
void enter() { today(); }
void frame() {
  Civil now = haveClock() ? civil(nowLocalMs()) : Civil{0, 0, 0, 0, 0, 0, 0, 0};
  card(4, APP_Y + 4, 232, 36);
  btn(8, APP_Y + 8, 36, 28, "<", C_CARD2, C_TEXT);
  btn(196, APP_Y + 8, 36, 28, ">", C_CARD2, C_TEXT);
  char b[32];
  snprintf(b, sizeof(b), "%s %d", MONTH_FULL[mon - 1], yr);
  text(b, 120, APP_Y + 22, &fonts::FreeSansBold9pt7b, C_TEXT, textdatum_t::middle_center);
  const char *WD[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
  for (int i = 0; i < 7; i++) text(WD[i], 18 + i * 34, APP_Y + 52, &fonts::Font2, i == 0 ? C_BAD : C_DIM, textdatum_t::middle_center);
  int first = dow(yr, mon, 1), n = daysIn(yr, mon);
  for (int d = 1; d <= n; d++) {
    int cell = first + d - 1, c = cell % 7, r = cell / 7;
    int cx = 18 + c * 34, cy = APP_Y + 78 + r * 34;
    bool isToday = now.y == yr && now.mo == mon && now.d == d;
    if (isToday) g->fillCircle(cx, cy, 15, accent());
    else g->fillRoundRect(cx - 15, cy - 14, 30, 28, 6, C_CARD);
    snprintf(b, sizeof(b), "%d", d);
    text(b, cx, cy, &fonts::Font2, isToday ? C_BLACK : c == 0 ? C_BAD : C_TEXT, textdatum_t::middle_center);
  }
  if (now.y) {
    snprintf(b, sizeof(b), "Today: %s %d %s %d", DAYS[now.wd], now.d, MONTHS[now.mo - 1], now.y);
    text(b, 120, H - 10, &fonts::Font2, C_DIM, textdatum_t::middle_center);
  }
}
void tap(int x, int y) {
  if (y > APP_Y + 44) return;
  if (x < 50) { if (--mon < 1) { mon = 12; yr--; } }
  else if (x > 190) { if (++mon > 12) { mon = 1; yr++; } }
  else today();
}
}  // namespace cal

// =============================================================== Simon (repeat the colour sequence)
namespace simon {
const uint16_t COL[4] = {rgb(40, 170, 80), rgb(210, 50, 50), rgb(230, 190, 30), rgb(40, 110, 230)};
const int QX[4] = {4, 122, 4, 122}, QY[4] = {APP_Y + 48, APP_Y + 48, APP_Y + 166, APP_Y + 166}, QS = 114;
uint8_t seq[64];
int len = 0, pos = 0, state = 0, lit = -1, showIdx = 0;  // state 0 idle, 1 showing, 2 your turn, 3 wrong
uint32_t until = 0, nextAt = 0;
bool best = false;
void startShow(uint32_t delayMs) { state = 1; showIdx = 0; lit = -1; nextAt = millis() + delayMs; }
void start() { len = 1; seq[0] = esp_random() % 4; best = false; startShow(600); }
void enter() { state = 0; lit = -1; }
void frame() {
  uint32_t now = millis();
  uint32_t onMs = max(180, 480 - len * 15);
  if (state == 1) {
    if (lit < 0 && now >= nextAt) { lit = seq[showIdx]; until = now + onMs; }
    else if (lit >= 0 && now >= until) {
      lit = -1;
      nextAt = now + 150;
      if (++showIdx >= len) { state = 2; pos = 0; }
    }
  } else if (lit >= 0 && now >= until) {
    lit = -1;
  }
  char l[20], r[20];
  snprintf(l, sizeof(l), "Level %d", state ? len : 0);
  snprintf(r, sizeof(r), "Best %lu", (unsigned long)hs[HS_SIMON]);
  statusBar(l, r);
  text(state == 1 ? "Watch..." : state == 2 ? "Your turn" : "", 120, APP_Y + 34, &fonts::Font2, C_DIM, textdatum_t::middle_center);
  for (int q = 0; q < 4; q++) g->fillRoundRect(QX[q], QY[q], QS, QS, 16, q == lit ? tg::lighten(COL[q], 0.55f) : tg::mix(COL[q], C_BG, 0.45f));
  g->fillCircle(120, QY[0] + QS + 2, 26, C_BG);
  g->fillCircle(120, QY[0] + QS + 2, 22, C_CARD2);
  snprintf(l, sizeof(l), "%d", len);
  text(state ? l : "GO", 120, QY[0] + QS + 2, &fonts::Font4, C_TEXT, textdatum_t::middle_center);
  if (state == 0) overlay("SIMON", "Repeat the colours. Tap to start", COL[3]);
  else if (state == 3) {
    snprintf(l, sizeof(l), "Reached level %d", len);
    overlay(best ? "NEW BEST!" : "WRONG!", l, best ? C_WARN : C_BAD);
  }
}
void down(int x, int y) {
  if (state == 0 || state == 3) { start(); return; }
  if (state != 2) return;
  for (int q = 0; q < 4; q++) {
    if (!hit(x, y, QX[q], QY[q], QS, QS)) continue;
    lit = q;
    until = millis() + 220;
    if (q != seq[pos]) { state = 3; best = hsBetter(HS_SIMON, len - 1, false); return; }
    if (++pos >= len) {
      hsBetter(HS_SIMON, len, false);
      if (len < 64) seq[len++] = esp_random() % 4;
      startShow(800);
    }
    return;
  }
}
}  // namespace simon

// =============================================================== Slide puzzle (15 tiles)
namespace slide {
uint8_t b[16];
int blank = 15, moves = 0, state = 0;  // 0 ready, 1 playing, 2 solved
uint32_t t0 = 0, t1 = 0;
bool init = false, best = false;
const int CS = 56, GAP = 3, BX = (W - (4 * CS + 3 * GAP)) / 2, BY = APP_Y + 44;
bool solved() {
  for (int i = 0; i < 15; i++) if (b[i] != i + 1) return false;
  return true;
}
void swapBlank(int i) { b[blank] = b[i]; b[i] = 0; blank = i; }
void shuffle() {
  for (int i = 0; i < 15; i++) b[i] = i + 1;
  b[15] = 0; blank = 15;
  int prev = -1;
  for (int k = 0; k < 300; k++) {  // random legal moves keep it solvable
    int r = blank / 4, c = blank % 4, opts[4], n = 0;
    if (r > 0) opts[n++] = blank - 4;
    if (r < 3) opts[n++] = blank + 4;
    if (c > 0) opts[n++] = blank - 1;
    if (c < 3) opts[n++] = blank + 1;
    int pick;
    do { pick = opts[esp_random() % n]; } while (pick == prev && n > 1);
    prev = blank;
    swapBlank(pick);
  }
  if (solved()) shuffle();
  moves = 0; state = 0; best = false; init = true;
}
void enter() { if (!init) shuffle(); }
void frame() {
  card(4, APP_Y + 4, 232, 34);
  char s[24];
  snprintf(s, sizeof(s), "Moves %d", moves);
  text(s, 12, APP_Y + 21, &fonts::Font2, C_TEXT, textdatum_t::middle_left);
  uint32_t secs = state == 0 ? 0 : ((state == 1 ? millis() : t1) - t0) / 1000;
  snprintf(s, sizeof(s), "%lus", (unsigned long)secs);
  text(s, 104, APP_Y + 21, &fonts::Font2, C_DIM, textdatum_t::middle_left);
  snprintf(s, sizeof(s), hs[HS_SLIDE] ? "best %lu" : "", (unsigned long)hs[HS_SLIDE]);
  text(s, 172, APP_Y + 21, &fonts::Font0, C_DIM, textdatum_t::middle_right);
  btn(178, APP_Y + 8, 54, 26, "New", APP_COLORS[A_SLIDE]);
  for (int i = 0; i < 16; i++) {
    if (!b[i]) continue;
    int x = BX + (i % 4) * (CS + GAP), y = BY + (i / 4) * (CS + GAP);
    bool home = b[i] == i + 1;
    uint16_t col = tg::mix(APP_COLORS[A_SLIDE], C_CPU, (b[i] - 1) / 15.0f);
    g->fillRoundRect(x, y, CS, CS, 8, home ? col : tg::mix(col, C_CARD, 0.45f));
    snprintf(s, sizeof(s), "%d", b[i]);
    text(s, x + CS / 2, y + CS / 2, &fonts::Font4, home ? C_BLACK : C_TEXT, textdatum_t::middle_center);
  }
  if (state == 2) {
    snprintf(s, sizeof(s), "%d moves, %lus", moves, (unsigned long)secs);
    overlay(best ? "NEW BEST!" : "SOLVED!", s, C_GOOD);
  }
}
void tap(int x, int y) {
  if (hit(x, y, 178, APP_Y + 4, 58, 34) || state == 2) { shuffle(); return; }
  if (x < BX || y < BY) return;
  int c = (x - BX) / (CS + GAP), r = (y - BY) / (CS + GAP);
  if (c > 3 || r > 3) return;
  int br = blank / 4, bc = blank % 4;
  if (r != br && c != bc) return;
  if (r == br && c == bc) return;
  if (state == 0) { state = 1; t0 = millis(); }
  while (blank != r * 4 + c) {  // slide every tile between the tapped one and the gap
    int step = r == br ? (c > blank % 4 ? 1 : -1) : (r > blank / 4 ? 4 : -4);
    swapBlank(blank + step);
  }
  moves++;
  if (solved()) { state = 2; t1 = millis(); best = hsBetter(HS_SLIDE, moves, true); }
}
}  // namespace slide

// =============================================================== Controllers page
namespace padsui {
const char *HOLD_NAMES[] = {"normal", "sideways L", "sideways R", "upside down"};
void frame() {
  card(4, APP_Y + 4, 232, 36);
  text("Bluetooth tilt controllers", 12, APP_Y + 22, &fonts::Font2, C_TEXT, textdatum_t::middle_left);
  btn(176, APP_Y + 9, 56, 26, st.padsOn ? "ON" : "OFF", st.padsOn ? C_GOOD : C_TRACK, st.padsOn ? C_BLACK : C_TEXT);
  for (int bd = 0; bd < 2; bd++) {
    int y = APP_Y + 44 + bd * 120;
    card(4, y, 232, 116);
    int player = bd == st.p1Board ? 1 : 2;
    char s[32];
    snprintf(s, sizeof(s), "Marble-P%d  =  %s", bd + 1, tg::pname(player));
    text(s, 12, y + 10, &fonts::Font2, tg::pcol(player), textdatum_t::middle_left);
    bool on = pads::connected(bd);
    const char *stt = on ? "connected" : !st.padsOn ? "off" : pads::pad[bd].linked ? "no data" : "searching";
    text(stt, 228, y + 10, &fonts::Font0, on ? C_GOOD : C_DIM, textdatum_t::middle_right);
    snprintf(s, sizeof(s), "Hold: %s", HOLD_NAMES[st.padHold[bd] % 4]);
    btn(10, y + 24, 132, 26, s, C_CARD2, C_TEXT);
    snprintf(s, sizeof(s), "Invert L/R: %s", st.padInvX[bd] ? "on" : "off");
    btn(10, y + 54, 132, 26, s, st.padInvX[bd] ? C_TRACK : C_CARD2, C_TEXT);
    snprintf(s, sizeof(s), "Invert U/D: %s", st.padInvY[bd] ? "on" : "off");
    btn(10, y + 84, 132, 26, s, st.padInvY[bd] ? C_TRACK : C_CARD2, C_TEXT);
    int bx = 152, by = y + 24, bs = 78, cx = bx + bs / 2, cy = by + bs / 2;  // live test box
    g->fillRoundRect(bx, by, bs, bs, 6, tg::FIELD);
    g->drawFastHLine(bx + 4, cy, bs - 8, C_TRACK);
    g->drawFastVLine(cx, by + 4, bs - 8, C_TRACK);
    float tx, ty;
    if (pads::boardTilt(bd, tx, ty)) g->fillCircle(cx + constrain(tx, -1.0f, 1.0f) * 32, cy + constrain(ty, -1.0f, 1.0f) * 32, 6, tg::pcol(player));
    else text("--", cx, cy, &fonts::Font2, C_DIM, textdatum_t::middle_center);
  }
  btn(4, H - 30, 114, 26, "Swap players", C_CARD2, C_TEXT);
  text("Tilt: the dot should follow", 178, H - 23, &fonts::Font0, C_DIM, textdatum_t::middle_center);
  text(pads::scanning ? "scanning..." : "BOOT on a pad = recalibrate", 178, H - 11, &fonts::Font0, C_DIM, textdatum_t::middle_center);
}
void tap(int x, int y) {
  if (hit(x, y, 172, APP_Y + 4, 64, 36)) st.padsOn ^= 1;
  else if (hit(x, y, 4, H - 32, 114, 30)) st.p1Board ^= 1;
  else {
    for (int bd = 0; bd < 2; bd++) {
      int yb = APP_Y + 44 + bd * 120;
      if (hit(x, y, 10, yb + 24, 132, 26)) st.padHold[bd] = (st.padHold[bd] + 1) % 4;
      else if (hit(x, y, 10, yb + 54, 132, 26)) st.padInvX[bd] ^= 1;
      else if (hit(x, y, 10, yb + 84, 132, 26)) st.padInvY[bd] ^= 1;
      else continue;
      break;
    }
  }
  saveSettings();
}
}  // namespace padsui

// =============================================================== Weather (from the PC server, Open-Meteo)
namespace wx {
enum Kind { K_SUN, K_PART, K_CLOUD, K_FOG, K_RAIN, K_SNOW, K_STORM };
Kind kindOf(int c) {
  if (c <= 1) return K_SUN;
  if (c == 2) return K_PART;
  if (c == 3) return K_CLOUD;
  if (c == 45 || c == 48) return K_FOG;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return K_SNOW;
  if (c >= 95) return K_STORM;
  return K_RAIN;
}
void cloud(int cx, int cy, int s, uint16_t col) {
  g->fillCircle(cx - s * 5 / 10, cy, s * 4 / 10, col);
  g->fillCircle(cx + s * 4 / 10, cy + s / 20, s * 35 / 100, col);
  g->fillCircle(cx - s / 20, cy - s * 3 / 10, s * 5 / 10, col);
  g->fillRoundRect(cx - s * 9 / 10, cy, s * 17 / 10, s * 4 / 10, s / 5, col);
}
void sun(int cx, int cy, int s, bool day) {
  if (!day) {  // crescent moon
    g->fillCircle(cx, cy, s * 6 / 10, rgb(230, 230, 200));
    g->fillCircle(cx + s * 3 / 10, cy - s * 2 / 10, s * 5 / 10, C_CARD);
    return;
  }
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    wline(cx + cosf(a) * s * 0.65f, cy + sinf(a) * s * 0.65f, cx + cosf(a) * s * 0.95f, cy + sinf(a) * s * 0.95f,
          fmaxf(1.0f, s / 14.0f), C_WARN);
  }
  g->fillCircle(cx, cy, s * 5 / 10, C_WARN);
}
// Weather icon about 2*s wide, centred on cx, cy (drawn on a C_CARD background).
void icon(int code, bool day, int cx, int cy, int s) {
  Kind k = kindOf(code);
  uint16_t cl = rgb(200, 205, 220), dark = rgb(120, 125, 145);
  switch (k) {
    case K_SUN: sun(cx, cy, s, day); break;
    case K_PART:
      sun(cx - s * 3 / 10, cy - s * 3 / 10, s * 7 / 10, day);
      cloud(cx + s / 10, cy + s * 2 / 10, s * 8 / 10, cl);
      break;
    case K_CLOUD: cloud(cx, cy, s, cl); break;
    case K_FOG:
      for (int i = 0; i < 4; i++)
        g->fillRoundRect(cx - s * 8 / 10 + (i % 2) * s / 5, cy - s / 2 + i * s / 3, s * 14 / 10, max(2, s / 8), 2, cl);
      break;
    default:  // rain, snow, storm
      cloud(cx, cy - s / 5, s * 9 / 10, k == K_STORM ? dark : cl);
      if (k == K_STORM) {
        g->fillTriangle(cx + s / 10, cy + s / 10, cx - s * 3 / 10, cy + s * 6 / 10, cx, cy + s * 6 / 10, C_WARN);
        g->fillTriangle(cx - s / 10, cy + s * 5 / 10, cx + s * 2 / 10, cy + s * 5 / 10, cx - s * 2 / 10, cy + s, C_WARN);
      } else {
        for (int i = -1; i <= 1; i++) {
          int x = cx + i * s * 4 / 10, y = cy + s * 4 / 10;
          if (k == K_SNOW) g->fillCircle(x, y + s / 6, max(1, s / 9), C_TEXT);
          else wline(x + s / 10, y, x - s / 10, y + s * 4 / 10, fmaxf(1.0f, s / 12.0f), C_CPU);
        }
      }
  }
}
void frame() {
  const auto &w = m.wx;
  if (!w.have) {
    card(4, APP_Y + 4, 232, 130);
    text("No weather yet", 120, APP_Y + 34, &fonts::Font4, C_TEXT, textdatum_t::middle_center);
    text("Set your city on the PC in", 120, APP_Y + 66, &fonts::Font2, C_DIM, textdatum_t::middle_center);
    text("windows_server\\settings.json", 120, APP_Y + 86, &fonts::Font2, C_DIM, textdatum_t::middle_center);
    text("then restart the server", 120, APP_Y + 106, &fonts::Font2, C_DIM, textdatum_t::middle_center);
    return;
  }
  char b[48];
  int y = APP_Y + 4;  // now
  card(4, y, 232, 112);
  snprintf(b, sizeof(b), "%s%s%s", w.city, w.country[0] ? ", " : "", w.country);
  text(b, 12, y + 6, &fonts::Font2, C_TEXT);
  snprintf(b, sizeof(b), "at %s", w.updated);
  text(b, 228, y + 8, &fonts::Font0, C_DIM, textdatum_t::top_right);
  icon(w.code, w.isDay, 52, y + 62, 30);
  tempText(w.temp, 156, y + 50, &fonts::FreeSansBold24pt7b, C_TEXT, true);
  text(w.desc, 156, y + 82, &fonts::Font2, C_DIM, textdatum_t::middle_center);
  snprintf(b, sizeof(b), "feels %.0f   humidity %d%%   wind %.0f km/h", tconv(w.feels), (int)w.hum, w.wind);
  text(b, 120, y + 101, &fonts::Font0, C_DIM, textdatum_t::middle_center);

  y = APP_Y + 120;  // next 12 hours
  card(4, y, 232, 88, "NEXT 12 HOURS");
  if (w.nh > 1) {
    float lo = 1e9, hi = -1e9;
    for (int i = 0; i < w.nh; i++) { lo = fminf(lo, w.hr[i].t); hi = fmaxf(hi, w.hr[i].t); }
    if (hi - lo < 4) { float mid = (hi + lo) / 2; lo = mid - 2; hi = mid + 2; }
    int px = -1, py = -1, step = 216 / (w.nh - 1);
    for (int i = 0; i < w.nh; i++) {
      int x = 12 + i * step, ty = y + 50 - (int)((w.hr[i].t - lo) / (hi - lo) * 26);
      if (px >= 0) { g->drawLine(px, py, x, ty, C_WARN); g->drawLine(px, py + 1, x, ty + 1, C_WARN); }
      g->fillCircle(x, ty, 2, C_WARN);
      if (i % 2 == 0) {
        snprintf(b, sizeof(b), "%.0f", tconv(w.hr[i].t));
        text(b, x, ty - 9, &fonts::Font0, C_TEXT, textdatum_t::middle_center);
        text(w.hr[i].h, x, y + 66, &fonts::Font0, C_DIM, textdatum_t::middle_center);
        if (w.hr[i].p >= 20) {
          snprintf(b, sizeof(b), "%d%%", w.hr[i].p);
          text(b, x, y + 78, &fonts::Font0, C_CPU, textdatum_t::middle_center);
        }
      }
      px = x;
      py = ty;
    }
  }

  y = APP_Y + 212;  // next 5 days
  card(4, y, 232, H - y - 4);
  float lo = 1e9, hi = -1e9;
  for (int i = 0; i < w.nd; i++) { lo = fminf(lo, w.dy[i].mn); hi = fmaxf(hi, w.dy[i].mx); }
  float span = fmaxf(1.0f, hi - lo);
  for (int i = 0; i < w.nd; i++) {
    int ry = y + 10 + i * 16;
    text(w.dy[i].day, 12, ry, &fonts::Font2, C_TEXT, textdatum_t::middle_left);
    icon(w.dy[i].c, true, 64, ry, 7);
    snprintf(b, sizeof(b), "%.0f", tconv(w.dy[i].mn));
    text(b, 96, ry, &fonts::Font0, C_DIM, textdatum_t::middle_right);
    int x0 = 100 + (int)((w.dy[i].mn - lo) / span * 80), x1 = 100 + (int)((w.dy[i].mx - lo) / span * 80);
    g->fillRoundRect(100, ry - 2, 84, 4, 2, C_TRACK);
    g->fillRoundRect(x0, ry - 2, max(4, x1 - x0 + 4), 4, 2, C_UP);
    snprintf(b, sizeof(b), "%.0f", tconv(w.dy[i].mx));
    text(b, 190, ry, &fonts::Font0, C_TEXT, textdatum_t::middle_left);
    if (w.dy[i].p >= 20) {
      snprintf(b, sizeof(b), "%d%%", w.dy[i].p);
      text(b, 228, ry, &fonts::Font0, C_CPU, textdatum_t::middle_right);
    }
  }
}
}  // namespace wx
