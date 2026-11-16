#pragma once
// LovyanGFX board definition: ESP32-S3 SuperMini + 2.8" 240x320 SPI TFT (ILI9341 or ST7789)
// with XPT2046 resistive touch sharing the same SPI bus. SD slot is not used.
// Pins come from config.h; defaults below match documentation/esp32s3_tft28.md.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define PCMX_PANEL_ILI9341 1
#define PCMX_PANEL_ST7789 2

#ifndef PCMONITORX_PANEL
#define PCMONITORX_PANEL PCMX_PANEL_ILI9341
#endif
#ifndef PCMONITORX_PANEL_INVERT
#define PCMONITORX_PANEL_INVERT 0
#endif
#ifndef PCMONITORX_PANEL_BGR
#define PCMONITORX_PANEL_BGR 0
#endif
#ifndef PCMONITORX_TOUCH_ENABLED
#define PCMONITORX_TOUCH_ENABLED 1
#endif
#ifndef PCMONITORX_ROTATION
#define PCMONITORX_ROTATION 0  // portrait; 2 = portrait upside-down
#endif
#ifndef PCMONITORX_SPI_MHZ
#define PCMONITORX_SPI_MHZ 40
#endif

#ifndef PCMONITORX_PIN_SCK
#define PCMONITORX_PIN_SCK 12
#endif
#ifndef PCMONITORX_PIN_MOSI
#define PCMONITORX_PIN_MOSI 11
#endif
#ifndef PCMONITORX_PIN_MISO
#define PCMONITORX_PIN_MISO 13
#endif
#ifndef PCMONITORX_PIN_TFT_CS
#define PCMONITORX_PIN_TFT_CS 10
#endif
#ifndef PCMONITORX_PIN_TFT_DC
#define PCMONITORX_PIN_TFT_DC 9
#endif
#ifndef PCMONITORX_PIN_TFT_RST
#define PCMONITORX_PIN_TFT_RST 8
#endif
#ifndef PCMONITORX_PIN_TFT_BL
#define PCMONITORX_PIN_TFT_BL -1  // -1 = LED wired to 3V3. Only use a GPIO if your board has a backlight transistor.
#endif
#ifndef PCMONITORX_PIN_TOUCH_CS
#define PCMONITORX_PIN_TOUCH_CS 6
#endif
#ifndef PCMONITORX_PIN_TOUCH_IRQ
#define PCMONITORX_PIN_TOUCH_IRQ 5
#endif

class LGFX_PCMX : public lgfx::LGFX_Device {
#if PCMONITORX_PANEL == PCMX_PANEL_ILI9341
  lgfx::Panel_ILI9341 _panel;
#elif PCMONITORX_PANEL == PCMX_PANEL_ST7789
  lgfx::Panel_ST7789 _panel;
#else
#error "PCMONITORX_PANEL must be PCMX_PANEL_ILI9341 or PCMX_PANEL_ST7789"
#endif
  lgfx::Bus_SPI _bus;
#if PCMONITORX_PIN_TFT_BL >= 0
  lgfx::Light_PWM _light;
#endif
#if PCMONITORX_TOUCH_ENABLED
  lgfx::Touch_XPT2046 _touch;
#endif

 public:
  LGFX_PCMX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = PCMONITORX_SPI_MHZ * 1000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PCMONITORX_PIN_SCK;
      cfg.pin_mosi = PCMONITORX_PIN_MOSI;
      cfg.pin_miso = PCMONITORX_PIN_MISO;
      cfg.pin_dc = PCMONITORX_PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = PCMONITORX_PIN_TFT_CS;
      cfg.pin_rst = PCMONITORX_PIN_TFT_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;  // never read pixels back; MISO is shared with touch
      cfg.invert = PCMONITORX_PANEL_INVERT;
      cfg.rgb_order = PCMONITORX_PANEL_BGR;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      _panel.config(cfg);
    }
#if PCMONITORX_PIN_TFT_BL >= 0
    {
      auto cfg = _light.config();
      cfg.pin_bl = PCMONITORX_PIN_TFT_BL;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#endif
#if PCMONITORX_TOUCH_ENABLED
    {
      auto cfg = _touch.config();
      cfg.x_min = 300;  // replaced by on-device calibration (Settings > Calibrate touch)
      cfg.x_max = 3800;
      cfg.y_min = 300;
      cfg.y_max = 3800;
      cfg.pin_int = PCMONITORX_PIN_TOUCH_IRQ;
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;
      cfg.spi_host = SPI2_HOST;
      cfg.freq = 2500000;
      cfg.pin_sclk = PCMONITORX_PIN_SCK;
      cfg.pin_mosi = PCMONITORX_PIN_MOSI;
      cfg.pin_miso = PCMONITORX_PIN_MISO;
      cfg.pin_cs = PCMONITORX_PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
#endif
    setPanel(&_panel);
  }
};
