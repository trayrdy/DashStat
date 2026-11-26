#pragma once
#include <stdint.h>

// ESP32 CYD (ESP32-2432S028R) Hardware Pinout

// Display SPI (ILI9341 on SPI2 / HSPI)
#define CYD_TFT_MOSI 13
#define CYD_TFT_MISO 12
#define CYD_TFT_SCLK 14
#define CYD_TFT_CS 15
#define CYD_TFT_DC 2
#define CYD_TFT_RST -1
#define CYD_TFT_BL 21

// Touch SPI (XPT2046 on independent GPIOs)
#define CYD_TOUCH_MOSI 32
#define CYD_TOUCH_MISO 39
#define CYD_TOUCH_SCLK 25
#define CYD_TOUCH_CS 33
#define CYD_TOUCH_IRQ 36

// MicroSD SPI (VSPI / SPI3_HOST)
#define CYD_SD_SCK 18
#define CYD_SD_MISO 19
#define CYD_SD_MOSI 23
#define CYD_SD_CS 5

// Onboard Peripherals
#define CYD_RGB_RED 4
#define CYD_RGB_GREEN 16
#define CYD_RGB_BLUE 17
#define CYD_LDR_PIN 34
#define CYD_BUZZER_PIN 26
