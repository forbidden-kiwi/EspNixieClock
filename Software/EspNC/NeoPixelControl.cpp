#include "NeoPixelControl.h"

// Initialize NeoPixel strip object (global)
Adafruit_NeoPixel strip(LED_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// LED brightness variables
int LedBrightness;                // Calculated brightness value (0-255)
int LedBrightnessPercentage;      // Brightness percentage (0-100)
uint8_t led_brightness_indx;      // Index for brightness intervals
const int led_brightness_num_intervals = 20;
const int led_brightness_intervals[20] = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 
                                          55, 60, 65, 70, 75, 80, 85, 90, 95, 100};

// LED effect and color variables
uint8_t LED_effect = 0;           // LED effect: 0=off, 1=rainbow, 2=color cycle, 3+=static color
uint8_t static_color_indx;        // Index for static color selection
const int static_color_num_colors = 12;
const uint32_t staticColors[12] = {
    strip.Color(255, 0, 0),    // Red
    strip.Color(250, 25, 0),   // Vermilion
    strip.Color(240, 50, 0),   // Orange
    strip.Color(210, 75, 0),   // Amber
    strip.Color(180, 140, 0),  // Yellow
    strip.Color(110, 200, 0),  // Chartreuse
    strip.Color(0, 255, 0),    // Green
    strip.Color(0, 150, 200),  // Teal
    strip.Color(0, 0, 255),    // Blue
    strip.Color(60, 0, 170),   // Violet
    strip.Color(130, 0, 180),  // Purple
    strip.Color(180, 0, 70)    // Magenta
};
uint8_t static_color;             // Current static color index

// Timing variables for effects
unsigned long rainbowCyclesPreviousMillis = 0;
unsigned long rainbowPreviousMillis = 0;
int rainbowCycles = 0;
int rainbowCycleCycles = 0;
unsigned long pixelsInterval = 50; // Interval between effect updates (ms)

/**
 * Initialize NeoPixel strip
 */
void initNeoPixels() {
    strip.begin();  // Initialize NeoPixel strip object
    strip.show();   // Turn off all pixels initially
}

/**
 * Update NeoPixel LEDs based on current effect and brightness settings
 */
void updateLEDs() {
    // Set brightness based on percentage
    LedBrightness = map(LedBrightnessPercentage, 0, 100, 0, 255);
    strip.setBrightness(LedBrightness);

    // Apply selected effect
    switch (LED_effect) {
        case 0: // Disabled
            strip.fill(0);
            break;

        case 1: // Rainbow cycle
            if (millis() - rainbowCyclesPreviousMillis >= pixelsInterval) {
                rainbowCyclesPreviousMillis = millis();
                rainbowCycle();
            }
            break;

        case 2: // Color cycle
            if (millis() - rainbowPreviousMillis >= pixelsInterval) {
                rainbowPreviousMillis = millis();
                rainbow();
            }
            break;

        default: // Static color (3 to 14)
            if (LED_effect >= 3 && LED_effect <= 14) {
                strip.fill(staticColors[LED_effect - 3]);
            }
            break;
    }

    strip.show(); // Update the strip with the new settings
}

/**
 * Display a rainbow cycle effect across all pixels
 */
void rainbowCycle() {
    for (uint16_t i = 0; i < strip.numPixels(); i++) {
        strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + rainbowCycleCycles) & 255));
    }
    strip.show();
    rainbowCycleCycles++;
    if (rainbowCycleCycles >= 256 * 5) rainbowCycleCycles = 0; // Reset after 5 full cycles
}

/**
 * Display a rainbow effect with uniform color shift
 */
void rainbow() {
    for (uint16_t i = 0; i < strip.numPixels(); i++) {
        strip.setPixelColor(i, Wheel((i + rainbowCycles) & 255));
    }
    strip.show();
    rainbowCycles++;
    if (rainbowCycles >= 256) rainbowCycles = 0; // Reset after one full cycle
}

/**
 * Generate a color value from a position in the color wheel
 * @param WheelPos Position in the color wheel (0-255)
 * @return 32-bit color value (RGB)
 */
uint32_t Wheel(byte WheelPos) {
    WheelPos = 255 - WheelPos;
    if (WheelPos < 85) {
        return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
    }
    if (WheelPos < 170) {
        WheelPos -= 85;
        return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
    }
    WheelPos -= 170;
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}