// ESP32-035 hardware probe. Read-only identification; no WiFi, no flash writes.
//
// What it does (serial monitor at 115200 baud):
//  1. Prints chip model/revision, flash size, PSRAM and MAC.
//  2. Reads the display controller ID registers (0x04, 0xD3, 0xDA-0xDC) over the
//     hypothesised SPI pins (SCLK14 MOSI13 MISO12 CS15 DC2, backlight 27).
//  3. Samples the XPT2046 touch controller (CS33, IRQ36) for 10 s: touch the screen.
//  4. Cycles three panel drivers (ST7796, ILI9488, ILI9341-320x480), 10 s each, drawing
//     an edge frame, corner labels and colour bars. Note which candidate looks correct.
//
// Pin source: https://github.com/littleCdev/ESP32-035 Hardware.md (unverified on your board).

#include <Arduino.h>
#include <SPI.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

static constexpr int P_SCLK = 14, P_MOSI = 13, P_MISO = 12, P_CS = 15, P_DC = 2, P_BL = 27;
static constexpr int P_TCS = 33, P_TIRQ = 36;

SPIClass probeSpi(HSPI);

static void lcdCmd(uint8_t c) {
  digitalWrite(P_DC, LOW);
  probeSpi.transfer(c);
  digitalWrite(P_DC, HIGH);
}

static void readReg(uint8_t reg, uint8_t n) {
  uint8_t buf[6] = {};
  probeSpi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(P_CS, LOW);
  lcdCmd(reg);
  for (uint8_t i = 0; i < n && i < sizeof(buf); i++) buf[i] = probeSpi.transfer(0x00);
  digitalWrite(P_CS, HIGH);
  probeSpi.endTransaction();
  Serial.printf("  reg 0x%02X ->", reg);
  for (uint8_t i = 0; i < n; i++) Serial.printf(" %02X", buf[i]);
  // Many controllers insert one dummy clock before multi-byte reads; show the 1-bit-shifted view too.
  Serial.print("   | shifted <<1:");
  for (uint8_t i = 0; i + 1 < n; i++) Serial.printf(" %02X", (uint8_t)((buf[i] << 1) | (buf[i + 1] >> 7)));
  Serial.println();
}

static void probeDisplayIds() {
  Serial.println("\n[2] Display controller ID registers");
  pinMode(P_CS, OUTPUT); digitalWrite(P_CS, HIGH);
  pinMode(P_TCS, OUTPUT); digitalWrite(P_TCS, HIGH);
  pinMode(P_DC, OUTPUT); digitalWrite(P_DC, HIGH);
  pinMode(P_BL, OUTPUT); digitalWrite(P_BL, HIGH);
  probeSpi.begin(P_SCLK, P_MISO, P_MOSI, -1);
  probeSpi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(P_CS, LOW); lcdCmd(0x01); digitalWrite(P_CS, HIGH);  // software reset
  probeSpi.endTransaction();
  delay(150);
  readReg(0x04, 4);  // RDDID
  readReg(0xD3, 4);  // ID4: ILI9488 -> 00 94 88, ST7796 -> 00 77 96 (after dummy)
  readReg(0xDA, 2);
  readReg(0xDB, 2);
  readReg(0xDC, 2);
  Serial.println("  Interpretation: look for 77 96 (ST7796), 94 88 (ILI9488), 93 41 (ILI9341).");
  Serial.println("  All 00 or all FF usually means MISO is not wired to the panel's SDO, or wrong pins.");
  probeSpi.end();
}

static uint16_t xptRead(uint8_t cmd) {
  probeSpi.transfer(cmd);
  uint16_t v = probeSpi.transfer16(0) >> 3;
  return v & 0x0FFF;
}

static void probeTouch() {
  Serial.println("\n[3] XPT2046 touch (CS33, IRQ36) - touch the screen corners now (10 s)");
  pinMode(P_TIRQ, INPUT);
  pinMode(P_CS, OUTPUT); digitalWrite(P_CS, HIGH);
  pinMode(P_TCS, OUTPUT); digitalWrite(P_TCS, HIGH);
  probeSpi.begin(P_SCLK, P_MISO, P_MOSI, -1);
  uint32_t until = millis() + 10000, samples = 0, irqLow = 0;
  uint16_t minX = 4095, maxX = 0, minY = 4095, maxY = 0;
  while (millis() < until) {
    bool irq = digitalRead(P_TIRQ) == LOW;
    probeSpi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(P_TCS, LOW);
    uint16_t z1 = xptRead(0xB1), z2 = xptRead(0xC1);
    uint16_t x = xptRead(0xD1), y = xptRead(0x91);
    xptRead(0x90);  // power down, IRQ enabled
    digitalWrite(P_TCS, HIGH);
    probeSpi.endTransaction();
    int z = z1 + 4095 - z2;
    if (irq) irqLow++;
    if (irq || (z1 > 100 && z < 4000)) {
      samples++;
      minX = min(minX, x); maxX = max(maxX, x); minY = min(minY, y); maxY = max(maxY, y);
      if (samples % 5 == 1) Serial.printf("  irq=%d x=%4u y=%4u z1=%4u z2=%4u\n", irq, x, y, z1, z2);
    }
    delay(40);
  }
  Serial.printf("  touch samples=%lu irqLow=%lu  rawX %u..%u  rawY %u..%u\n", (unsigned long)samples,
                (unsigned long)irqLow, minX, maxX, minY, maxY);
  Serial.println(samples ? "  Touch controller responded. Report the raw ranges above."
                         : "  No touch detected. Either not touched, or touch wiring differs from CS33/IRQ36.");
  probeSpi.end();
}

// One LGFX device whose panel type is chosen at runtime by index.
template <class PanelT>
class ProbeLGFX : public lgfx::LGFX_Device {
  PanelT _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  ProbeLGFX() {
    auto b = _bus.config();
    b.spi_host = HSPI_HOST; b.spi_mode = 0; b.freq_write = 27000000; b.freq_read = 16000000;
    b.spi_3wire = false; b.use_lock = true; b.dma_channel = SPI_DMA_CH_AUTO;
    b.pin_sclk = P_SCLK; b.pin_mosi = P_MOSI; b.pin_miso = P_MISO; b.pin_dc = P_DC;
    _bus.config(b);
    _panel.setBus(&_bus);
    auto p = _panel.config();
    p.pin_cs = P_CS; p.pin_rst = -1; p.pin_busy = -1;
    p.panel_width = 320; p.panel_height = 480; p.memory_width = 320; p.memory_height = 480;
    p.readable = true; p.invert = false; p.rgb_order = false; p.dlen_16bit = false; p.bus_shared = true;
    _panel.config(p);
    auto l = _light.config();
    l.pin_bl = P_BL; l.invert = false; l.freq = 12000; l.pwm_channel = 7;
    _light.config(l);
    _panel.setLight(&_light);
    setPanel(&_panel);
  }
};

template <class PanelT>
static void showCandidate(uint8_t idx, const char *name) {
  Serial.printf("\n[4.%u] Candidate %u: %s  (watch the screen for 10 s)\n", idx, idx, name);
  auto *d = new ProbeLGFX<PanelT>();
  d->init();
  d->setRotation(1);
  d->setBrightness(255);
  int w = d->width(), h = d->height();
  d->fillScreen(TFT_BLACK);
  const uint32_t bars[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK};
  const char *names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
  int bw = (w - 40) / 5;
  for (int i = 0; i < 5; i++) {
    d->fillRect(20 + i * bw, h / 2 - 10, bw - 4, 70, bars[i]);
    d->drawRect(20 + i * bw, h / 2 - 10, bw - 4, 70, TFT_DARKGREY);
    d->setTextColor(TFT_WHITE, TFT_BLACK);
    d->setTextDatum(textdatum_t::top_center);
    d->setFont(&fonts::Font2);
    d->drawString(names[i], 20 + i * bw + (bw - 4) / 2, h / 2 + 66);
  }
  d->drawRect(0, 0, w, h, TFT_WHITE);       // 1-px frame on the outermost pixels
  d->drawRect(2, 2, w - 4, h - 4, TFT_YELLOW);
  d->setFont(&fonts::Font4);
  d->setTextDatum(textdatum_t::top_center);
  char title[48];
  snprintf(title, sizeof(title), "Candidate %u: %s", idx, name);
  d->drawString(title, w / 2, 24);
  char dims[32];
  snprintf(dims, sizeof(dims), "%d x %d (landscape)", w, h);
  d->setFont(&fonts::Font2);
  d->drawString(dims, w / 2, 56);
  d->setTextDatum(textdatum_t::top_left);     d->drawString("TL", 8, 8);
  d->setTextDatum(textdatum_t::top_right);    d->drawString("TR", w - 8, 8);
  d->setTextDatum(textdatum_t::bottom_left);  d->drawString("BL", 8, h - 8);
  d->setTextDatum(textdatum_t::bottom_right); d->drawString("BR", w - 8, h - 8);
  Serial.printf("  drawn %dx%d. Correct = sharp text, labels match colours, white frame on all 4 edges.\n", w, h);
  delay(10000);
  d->fillScreen(TFT_BLACK);
  d->releaseBus();  // free HSPI so the next candidate / raw probe can claim it
  delete d;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n===== ESP32-035 probe =====");
  Serial.println("[1] Chip");
  Serial.printf("  model %s rev %u, %u cores, %u MHz\n", ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("  flash %u MB, PSRAM %u KB, free heap %u KB\n", ESP.getFlashChipSize() / 1048576,
                ESP.getPsramSize() / 1024, ESP.getFreeHeap() / 1024);
  Serial.printf("  MAC %012llX, SDK %s\n", ESP.getEfuseMac(), ESP.getSdkVersion());
}

void loop() {
  probeDisplayIds();
  probeTouch();
  showCandidate<lgfx::Panel_ST7796>(1, "ST7796");
  showCandidate<lgfx::Panel_ILI9488>(2, "ILI9488");
  showCandidate<lgfx::Panel_ILI9341>(3, "ILI9341 (320x480 init)");
  Serial.println("\n===== cycle complete; repeating =====");
}
