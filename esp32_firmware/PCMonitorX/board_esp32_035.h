#pragma once
// ESP32-035 board definition for LovyanGFX.
//
// PIN SOURCE: https://github.com/littleCdev/ESP32-035 (Hardware.md), retrieved 2026-11-13.
// STATUS: hypothesis. These pins have NOT been verified on your board yet. Run
// esp32_firmware/diagnostics/ESP32_035_Probe first and record the results in
// hardware/esp32_035_board.md before trusting them.
//
//   Display SPI (HSPI): SCLK 14, MOSI 13, MISO 12, CS 15, DC 2, RST tied to EN (-1)
//   Backlight:          GPIO27 (drives transistor Q2, active HIGH)
//   Touch XPT2046:      same SPI bus, CS 33, PENIRQ 36
//   (Not used here: TF card CS5/MOSI23/SCK18/MISO19; RGB LED 4/16/17)

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define PCMX_PANEL_ST7796 1
#define PCMX_PANEL_ILI9488 2
#define PCMX_PANEL_ILI9341 3  // 320x480 init used by the littleCdev examples (ILI9341_2 in TFT_eSPI)

#ifndef PCMONITORX_PANEL
#error "Set PCMONITORX_PANEL in config.h to the controller confirmed by the ESP32_035_Probe sketch."
#endif
#ifndef PCMONITORX_PANEL_INVERT
#define PCMONITORX_PANEL_INVERT 0
#endif
#ifndef PCMONITORX_PANEL_BGR
#define PCMONITORX_PANEL_BGR 0
#endif
#ifndef PCMONITORX_TOUCH_ENABLED
#define PCMONITORX_TOUCH_ENABLED 0
#endif

namespace pcmx_pins {
constexpr int SCLK = 14, MOSI = 13, MISO = 12, CS = 15, DC = 2, RST = -1, BL = 27;
constexpr int TOUCH_CS = 33, TOUCH_IRQ = 36;
constexpr int BOOT_BUTTON = 0;
}

class LGFX_ESP32_035 : public lgfx::LGFX_Device {
#if PCMONITORX_PANEL == PCMX_PANEL_ST7796
  lgfx::Panel_ST7796 _panel;
#elif PCMONITORX_PANEL == PCMX_PANEL_ILI9488
  lgfx::Panel_ILI9488 _panel;
#elif PCMONITORX_PANEL == PCMX_PANEL_ILI9341
  lgfx::Panel_ILI9341 _panel;
#else
#error "PCMONITORX_PANEL must be PCMX_PANEL_ST7796, PCMX_PANEL_ILI9488 or PCMX_PANEL_ILI9341."
#endif
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
#if PCMONITORX_TOUCH_ENABLED
  lgfx::Touch_XPT2046 _touch;
#endif

 public:
  LGFX_ESP32_035() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;  // reference uses 55 MHz; 40 MHz leaves margin
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = pcmx_pins::SCLK;
      cfg.pin_mosi = pcmx_pins::MOSI;
      cfg.pin_miso = pcmx_pins::MISO;
      cfg.pin_dc = pcmx_pins::DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = pcmx_pins::CS;
      cfg.pin_rst = pcmx_pins::RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.memory_width = 320;
      cfg.memory_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = PCMONITORX_PANEL_INVERT;
      cfg.rgb_order = PCMONITORX_PANEL_BGR;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;  // touch controller shares this SPI bus
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = pcmx_pins::BL;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#if PCMONITORX_TOUCH_ENABLED
    {
      auto cfg = _touch.config();
      // Placeholder raw range; replace with Settings > Calibrate touch on the device.
      cfg.x_min = 200;
      cfg.x_max = 3900;
      cfg.y_min = 200;
      cfg.y_max = 3900;
      cfg.pin_int = pcmx_pins::TOUCH_IRQ;
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;
      cfg.spi_host = HSPI_HOST;
      cfg.freq = 2500000;
      cfg.pin_sclk = pcmx_pins::SCLK;
      cfg.pin_mosi = pcmx_pins::MOSI;
      cfg.pin_miso = pcmx_pins::MISO;
      cfg.pin_cs = pcmx_pins::TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
#endif
    setPanel(&_panel);
  }
};
