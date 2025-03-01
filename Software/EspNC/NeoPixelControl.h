#ifndef NEOPIXEL_CONTROL_H
#define NEOPIXEL_CONTROL_H

#include <Adafruit_NeoPixel.h>
#include "Globals.h" // Provides PIN_NEOPIXEL and LED_COUNT

// External declarations for NeoPixel strip and control variables
extern Adafruit_NeoPixel strip;       // NeoPixel strip object

extern int LedBrightness;             // Calculated brightness value (0-255)
extern int LedBrightnessPercentage;   // Brightness percentage (0-100)
extern uint8_t led_brightness_indx;   // Index for brightness intervals
extern const int led_brightness_num_intervals;
extern const int led_brightness_intervals[20];

extern uint8_t LED_effect;            // LED effect: 0=disabled, 1=rainbow, 2=color cycle, 3+=static color
extern uint8_t static_color_indx;     // Index for static color selection
extern const int static_color_num_colors;
extern const uint32_t staticColors[12];
extern uint8_t static_color;          // Current static color index

extern unsigned long rainbowCyclesPreviousMillis; // Timestamp for rainbow cycle updates
extern unsigned long rainbowPreviousMillis;       // Timestamp for rainbow updates
extern int rainbowCycles;                         // Current cycle count for rainbow effect
extern int rainbowCycleCycles;                    // Current cycle count for rainbow cycle effect
extern unsigned long pixelsInterval;              // Time interval between effect updates (ms)

// Function prototypes
/**
 * Initialize NeoPixel strip
 */
void initNeoPixels();

/**
 * Update NeoPixel LEDs based on current effect and brightness settings
 */
void updateLEDs();

/**
 * Display a rainbow cycle effect across all pixels
 */
void rainbowCycle();

/**
 * Display a rainbow effect with uniform color shift
 */
void rainbow();

/**
 * Generate a color value from a position in the color wheel
 * @param WheelPos Position in the color wheel (0-255)
 * @return 32-bit color value (RGB)
 */
uint32_t Wheel(byte WheelPos);

#endif // NEOPIXEL_CONTROL_H