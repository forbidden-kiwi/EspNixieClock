#ifndef Globals_h
#define Globals_h

// Pin definitions for HV5622 shift registers
#define PIN_HV_LE 0     // Latch enable pin
#define PIN_HV_BL 2     // Blank pin
#define PIN_HV_DATA 12  // Data pin
#define PIN_HV_CLK 13   // Clock pin

// Pin definition for DC/DC 170V enable
#define PIN_HV_EN 14    // Enable pin for high voltage supply

// Pin definition and count for NeoPixel LEDs
#define PIN_NEOPIXEL 10 // NeoPixel data pin
#define LED_COUNT 12    // Number of NeoPixels

#endif
