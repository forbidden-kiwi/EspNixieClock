/*
 * ESP Nixie Clock
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License <http://www.gnu.org/licenses/> for more details.
 */

#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <Wire.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <avdweb_Switch.h>
#include <ALib0.h>

#include "NixieDisplay.h"
#include "HvSupply.h"
#include "Globals.h"
#include "NeoPixelControl.h"

// WiFi configuration
#define AP_NAME "NixieClock"
#define AP_PASSWORD "password"

// OLED display configuration
#define OLED_RESET LED_BUILTIN
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Encoder and button pins
const int encoderPinA = 3;
const int encoderPinB = 1;
const int encoderButtonPin = 16;

// Colon settings
#define CLOCK_COLON // Uncomment if not using colon separator PCB
const int enableBlink_num_state = 4;
const int enableBlink_state[enableBlink_num_state] = {0, 1, 2, 3}; // Off, slow blink, fast blink, always on

// Timezone definitions
#define TZ_EUROPE 0     // Last Sunday March to last Sunday October
#define TZ_USA 1        // Second Sunday March to first Sunday November
#define TZ_AUSTRALIA 2  // First Sunday October to first Sunday April
#define TZ_NEWZEALAND 3 // Last Sunday September to first Sunday April
#define TZ_CHILE 4      // First Sunday September to first Sunday April

// DST and STD rules
TimeChangeRule dstRules[] = {
    {"DST", Last, Sun, Mar, 2, 0},   // Europe
    {"DST", Second, Sun, Mar, 2, 0}, // USA/Canada
    {"DST", First, Sun, Oct, 2, 0},  // Australia
    {"DST", Last, Sun, Sep, 2, 0},   // New Zealand
    {"DST", First, Sun, Sep, 2, 0},  // Chile
};

TimeChangeRule stdRules[] = {
    {"STD", Last, Sun, Oct, 2, 0},  // Europe
    {"STD", First, Sun, Nov, 2, 0}, // USA/Canada
    {"STD", First, Sun, Apr, 2, 0}, // Australia
    {"STD", First, Sun, Apr, 2, 0}, // New Zealand
    {"STD", First, Sun, Apr, 2, 0}, // Chile
};

// EEPROM Addresses
const int EEPROM_addr_UTC_offset = 0;
const int EEPROM_addr_DST = 1;
const int EEPROM_addr_12_24 = 2;
const int EEPROM_addr_protect = 3;
const int EEPROM_addr_shutoff_en = 4;
const int EEPROM_addr_shutoff_off = 5;
const int EEPROM_addr_shutoff_on = 6;
const int EEPROM_addr_showzero = 7;
const int EEPROM_addr_blink_colon = 8;
const int EEPROM_addr_screensaver = 9;
const int EEPROM_addr_led = 10;
const int EEPROM_addr_showdate = 11;
const int EEPROM_addr_showdate_second = 12;
const int EEPROM_addr_showdate_interval = 13;
const int EEPROM_addr_showdate_duration = 14;
const int EEPROM_addr_static_color = 15;
const int EEPROM_addr_led_brightness = 16;
const int EEPROM_addr_DST_rule = 17;
const int EEPROM_addr_date_format = 18;

// Global variables
bool wifiConnected = false;        // WiFi connection status
uint8_t enableBlink_indx;          // Colon blink index
uint8_t enableBlink;               // Colon blink state
NixieDisplay nixie;                // Nixie display object
HvSupply hv_supply;                // High voltage supply object
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time.nist.gov", 0, 7200000); // NTP client, updates every 2 hours
TimeChangeRule myDST = {"DST", Last, Sun, Mar, 2, 0};
TimeChangeRule mySTD = {"STD", Last, Sun, Oct, 2, 0};
uint8_t currentTimeZone = TZ_EUROPE;
Timezone myTZ(myDST, mySTD);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
const int debouncePeriod = 50;
const int longPressPeriod = 3000; // 3 seconds for manual override
Switch encoderButton = Switch(encoderButtonPin, INPUT_PULLUP, LOW, debouncePeriod, longPressPeriod);
int encoderPos, encoderPosPrev;
bool transitionToDate = true;
bool transitionFromDate = true;
int waitTime = 100; // Animation delay in milliseconds

// Menu enumeration
enum Menu {
    TOP,
    SETTINGS1,
    SETTINGS2,
    SETTINGS3,
    SET_UTC_OFFSET,
    DST_MENU,
    ENABLE_DST,
    DST_RULE,
    SET_12_24,
    BLINK_COLON,
    CATHODE_PROTECT,
    AUTO_SHUTOFF,
    AUTO_SHUTOFF_ENABLE,
    AUTO_SHUTOFF_OFFTIME,
    AUTO_SHUTOFF_ONTIME,
    LED_MENU,
    STATIC_COLOR,
    LED_BRIGHTNESS,
    SHOW_ZERO,
    SHOW_DATE,
    SHOW_DATE_MENU,
    SHOW_DATE_SECOND,
    SHOW_DATE_INTERVAL,
    SHOW_DATE_DURATION,
    SHOW_DATE_FORMAT,
    SCREENSAVER_MENU,
    SCREENSAVER,
    SET_TIME,
    SET_TIME_MANUALLY,
    SET_TIME_WIFI,
    RESET_WIFI
} menu;

// Configuration variables
bool enableDST;                    // Enable daylight saving time
bool set12_24;                     // 12-hour (0) or 24-hour (1) format
bool showZero;                     // Show leading zero in hours
bool showDate;                     // Show date on Nixie tubes
uint8_t ssOption;                  // Screensaver option: 0=off, 1=scrolling, 2=OLED off

uint8_t interval_indx;             // Cathode protection interval index
const int num_intervals = 8;
const int intervals[num_intervals] = {0, 1, 5, 10, 15, 20, 30, 60}; // Protection intervals in minutes

uint8_t showdate_interval;         // Date display interval
uint8_t date_interval_indx;        // Date interval index
const int date_num_intervals = 6;
const int date_intervals[date_num_intervals] = {1, 2, 5, 10, 15, 30}; // Intervals in minutes
uint8_t showdate_duration;         // Date display duration
uint8_t date_duration_indx;        // Date duration index
const int date_duration_num_intervals = 8;
const int date_duration_intervals[date_duration_num_intervals] = {3, 4, 5, 6, 7, 8, 9, 10}; // Duration in seconds
uint8_t showdate_second;           // Second to start showing date
uint8_t dateFormat;                // Date format: 0=DD:MM:YY, 1=MM:DD:YY, 2=YY:MM:DD
uint8_t date_format_indx;          // Date format index
const int date_format_num_intervals = 3;
const int date_format_intervals[date_format_num_intervals] = {0, 1, 2};

bool enableAutoShutoff;            // Enable auto shutoff
int autoShutoffOfftime;            // Shutoff time (0-95, 15-minute intervals)
int autoShutoffOntime;             // Turn-on time (0-95, 15-minute intervals)
time_t protectTimer = 0;           // Cathode protection timer
time_t menuTimer = 0;              // Menu timeout timer
bool nixieOn = true;               // Nixie tubes on/off state
bool manualOverride = false;       // Manual override for shutoff
bool initProtectionTimer = false;  // Sync protection timer at top of hour
static bool firstEntry = true;
static int setHour = 0, setMinute = 0, setDay = 1, setMonth = 1, setYear = 2023, setAmPm = 0;
static int field = 0;
static const char* amPmLabels[] = {"AM", "PM"};

// Screensaver bitmaps
#define NIXIE_HEIGHT 24
#define NIXIE_WIDTH 17

static const unsigned char PROGMEM NixieN_bmp[] = {
    B00000000, B10000000, B00000000, // ........#...............
    B00000001, B11000000, B00000000, // .......###..............
    B00000001, B01000000, B00000000, // .......#.#..............
    B00000011, B01100000, B00000000, // ......##.##.............
    B00000110, B00110000, B00000000, // .....##...##............
    B00011100, B00011100, B00000000, // ...###.....###..........
    B00010000, B00000100, B00000000, // ...#.........#..........
    B00010001, B00100100, B00000000, // ...#...#..#..#..........
    B00010011, B00100100, B00000000, // ...#..##..#..#..........
    B00010011, B00100100, B00000000, // ...#..##..#..#..........
    B00010011, B00100100, B00000000, // ...#..##..#..#..........
    B00010011, B10100100, B00000000, // ...#..###.#..#..........
    B00010011, B10100100, B00000000, // ...#..###.#..#..........
    B00010010, B10100100, B00000000, // ...#..#.#.#..#..........
    B00010010, B11100100, B00000000, // ...#..#.###..#..........
    B00010010, B11100100, B00000000, // ...#..#.###..#..........
    B00010010, B01100100, B00000000, // ...#..#..##..#..........
    B00010010, B01100100, B00000000, // ...#..#..##..#..........
    B00010010, B01000100, B00000000, // ...#..#..#...#..........
    B00010000, B00000100, B00000000, // ...#.........#..........
    B00001111, B11111000, B00000000, // ....#########...........
    B00000101, B01010000, B00000000, // .....#.#.#.#............
    B00000101, B01010000, B00000000, // .....#.#.#.#............
    B00000101, B01010000, B00000000  // .....#.#.#.#............
};

static const unsigned char PROGMEM NixieI_bmp[] = {
    B00000000, B10000000, B00000000, // ........#...............
    B00000001, B11000000, B00000000, // .......###..............
    B00000001, B01000000, B00000000, // .......#.#..............
    B00000011, B01100000, B00000000, // ......##.##.............
    B00000110, B00110000, B00000000, // .....##...##............
    B00011100, B00011100, B00000000, // ...###.....###..........
    B00010000, B00000100, B00000000, // ...#.........#..........
    B00010011, B11100100, B00000000, // ...#..#####..#..........
    B00010001, B11000100, B00000000, // ...#...###...#..........
    B00010000, B10000100, B00000000, // ...#....#....#..........
    B00010000, B10000100, B00000000, // ...#....#....#..........
    B00010000, B10000100, B00000000, // ...#....#....#..........
    B00010000, B10000100, B00000000, // ...#....#....#..........
    B00010000, B10000100, B00000000, // ...#....#....#..........
    B00010000, B10000100, B00000000, // ...#....#....#..........
    B00010000, B10000100, B00000000, // ...#....#....#..........
    B00010000, B10000100, B00000000, // ...#....#....#..........
    B00010001, B11000100, B00000000, // ...#...###...#..........
    B00010011, B11100100, B00000000, // ...#..#####..#..........
    B00010000, B00000100, B00000000, // ...#.........#..........
    B00001111, B11111000, B00000000, // ....#########...........
    B00000101, B01010000, B00000000, // .....#.#.#.#............
    B00000101, B01010000, B00000000, // .....#.#.#.#............
    B00000101, B01010000, B00000000  // .....#.#.#.#............
};

static const unsigned char PROGMEM NixieX_bmp[] = {
    B00000000, B10000000, B00000000, // ........#...............
    B00000001, B11000000, B00000000, // .......###..............
    B00000001, B01000000, B00000000, // .......#.#..............
    B00000011, B01100000, B00000000, // ......##.##.............
    B00000110, B00110000, B00000000, // .....##...##............
    B00011100, B00011100, B00000000, // ...###.....###..........
    B00010000, B00000100, B00000000, // ...#.........#..........
    B00010010, B00100100, B00000000, // ...#..#...#..#..........
    B00010010, B00100100, B00000000, // ...#..#...#..#..........
    B00010011, B01100100, B00000000, // ...#..##.##..#..........
    B00010001, B01000100, B00000000, // ...#...#.#...#..........
    B00010001, B11000100, B00000000, // ...#...###...#..........
    B00010000, B10000100, B00000000, // ...#....#....#..........
    B00010000, B10000100, B00000000, // ...#....#....#..........
    B00010001, B11000100, B00000000, // ...#...###...#..........
    B00010001, B01000100, B00000000, // ...#...#.#...#..........
    B00010011, B01100100, B00000000, // ...#..##.##..#..........
    B00010010, B00100100, B00000000, // ...#..#...#..#..........
    B00010010, B00100100, B00000000, // ...#..#...#..#..........
    B00010000, B00000100, B00000000, // ...#.........#..........
    B00001111, B11111000, B00000000, // ....#########...........
    B00000101, B01010000, B00000000, // .....#.#.#.#............
    B00000101, B01010000, B00000000, // .....#.#.#.#............
    B00000101, B01010000, B00000000  // .....#.#.#.#............
};

static const unsigned char PROGMEM NixieE_bmp[] = {
    B00000000, B10000000, B00000000, // ........#...............
    B00000001, B11000000, B00000000, // .......###..............
    B00000001, B01000000, B00000000, // .......#.#..............
    B00000011, B01100000, B00000000, // ......##.##.............
    B00000110, B00110000, B00000000, // .....##...##............
    B00011100, B00011100, B00000000, // ...###.....###..........
    B00010000, B00000100, B00000000, // ...#.........#..........
    B00010001, B11100100, B00000000, // ...#...####..#..........
    B00010011, B11000100, B00000000, // ...#..####...#..........
    B00010011, B00000100, B00000000, // ...#..##.....#..........
    B00010011, B00000100, B00000000, // ...#..##.....#..........
    B00010011, B00000100, B00000000, // ...#..##.....#..........
    B00010011, B11000100, B00000000, // ...#..####...#..........
    B00010011, B10000100, B00000000, // ...#..###....#..........
    B00010011, B00000100, B00000000, // ...#..##.....#..........
    B00010011, B00000100, B00000000, // ...#..##.....#..........
    B00010011, B00000100, B00000000, // ...#..##.....#..........
    B00010011, B11000100, B00000000, // ...#..####...#..........
    B00010001, B11100100, B00000000, // ...#...####..#..........
    B00010000, B00000100, B00000000, // ...#.........#..........
    B00001111, B11111000, B00000000, // ....#########...........
    B00000101, B01010000, B00000000, // .....#.#.#.#............
    B00000101, B01010000, B00000000, // .....#.#.#.#............
    B00000101, B01010000, B00000000  // .....#.#.#.#............
};

/**
 * Setup function to initialize hardware and settings
 */
void setup() {
    // Configure encoder pins
    pinMode(encoderPinA, INPUT_PULLUP);
    pinMode(encoderPinB, INPUT_PULLUP);

    // Initialize OLED display
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // OLED I2C Address, may need adjustment
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.clearDisplay();

    // Show initial connection message
    display.setCursor(0, 0);
    display.println("Connecting...");
    display.println();
    display.println("Cycle Power after a");
    display.println("few minutes if no");
    display.print("connection.");
    display.display();

    // Configure WiFi using WiFiManager
    WiFiManager MyWifiManager;
    MyWifiManager.setAPCallback(configModeCallback);
    MyWifiManager.setTimeout(180);
    wifiConnected = MyWifiManager.autoConnect(AP_NAME, AP_PASSWORD);

    // Display WiFi connection status
    display.clearDisplay();
    display.setCursor(0, 0);
    if (wifiConnected) {
        display.print("Wifi Connected");
    } else {
        display.print("No Wifi Available");
        display.setCursor(0, 16);
        display.println("Running without time");
        display.println("Set Wifi in Settings");
    }
    display.display();
    delay(2000);

    // Start NTP client
    timeClient.begin();

    // Update local time
    display.setCursor(0, 28);
    display.println("Updating local time");
    display.display();

    unsigned long startTime = millis();
    while (!timeClient.update() && millis() - startTime < 5000) {
        delay(500);
        display.print(".");
        display.display();
    }

    if (timeClient.update() && wifiConnected) {
        setTime(myTZ.toLocal(timeClient.getEpochTime()));
    } else {
        setTime(0);
    }

    // Initialize EEPROM
    EEPROM.begin(512);

    // Load settings from EEPROM with validation
    int utc_offset = (((int)EEPROM.read(EEPROM_addr_UTC_offset) + 12) % 24) - 12;

    currentTimeZone = EEPROM.read(EEPROM_addr_DST_rule);
    if (currentTimeZone > TZ_CHILE) currentTimeZone = TZ_EUROPE;

    enableDST = EEPROM.read(EEPROM_addr_DST) != 0;
    if (enableDST > 1) enableDST = 0;

    set12_24 = EEPROM.read(EEPROM_addr_12_24) != 0;
    if (set12_24 > 1) set12_24 = 1;

    interval_indx = EEPROM.read(EEPROM_addr_protect);
    if (interval_indx >= num_intervals) interval_indx = 0;
    
    enableAutoShutoff = EEPROM.read(EEPROM_addr_shutoff_en) != 0;
    if (enableAutoShutoff > 1) enableAutoShutoff = 0;
    autoShutoffOfftime = (int)EEPROM.read(EEPROM_addr_shutoff_off);
    autoShutoffOntime = (int)EEPROM.read(EEPROM_addr_shutoff_on);
    if (autoShutoffOfftime > 95) autoShutoffOfftime = 88; // Default to 22:00
    if (autoShutoffOntime > 95) autoShutoffOntime = 24;   // Default to 06:00

    showZero = EEPROM.read(EEPROM_addr_showzero) != 0;
    if (showZero > 1) showZero = 0;

    showDate = EEPROM.read(EEPROM_addr_showdate) != 0;
    if (showDate > 1) showDate = 0;

    showdate_interval = EEPROM.read(EEPROM_addr_showdate_interval);
    if (showdate_interval >= date_num_intervals) showdate_interval = 0;

    static_color = EEPROM.read(EEPROM_addr_static_color);
    if (static_color >= static_color_num_colors) static_color = 2;

    showdate_second = EEPROM.read(EEPROM_addr_showdate_second);
    if (showdate_second > 50) showdate_second = 45;

    showdate_duration = EEPROM.read(EEPROM_addr_showdate_duration);
    if (showdate_duration >= date_duration_num_intervals) showdate_duration = 0;

    enableBlink = EEPROM.read(EEPROM_addr_blink_colon);
    if (enableBlink >= enableBlink_num_state) enableBlink = 0;
    enableBlink_indx = enableBlink;

    ssOption = EEPROM.read(EEPROM_addr_screensaver);
    if (ssOption > 2) ssOption = 0;

    LED_effect = EEPROM.read(EEPROM_addr_led);
    if (LED_effect > 14) LED_effect = 1;

    uint8_t brightnessIndex = EEPROM.read(EEPROM_addr_led_brightness);
    if (brightnessIndex >= led_brightness_num_intervals) brightnessIndex = 0;
    LedBrightnessPercentage = led_brightness_intervals[brightnessIndex];
    led_brightness_indx = brightnessIndex;

    dateFormat = EEPROM.read(EEPROM_addr_date_format);
    if (dateFormat > 2) dateFormat = 0;

    // Configure timezone
    mySTD = stdRules[currentTimeZone];
    myDST = dstRules[currentTimeZone];
    mySTD.offset = utc_offset * 60;
    myDST.offset = mySTD.offset;
    if (enableDST) myDST.offset += 60;
    myTZ = Timezone(myDST, mySTD);

    // Initialize components
    initNeoPixels();
    menu = TOP;
    updateSelection();
    timeClient.update();
    setTime(myTZ.toLocal(timeClient.getEpochTime()));
    hv_supply.begin();
    nixie.begin();
    displayTime();
    menuTimer = now();
}

/**
 * Main loop function
 */
void loop() {
    // Update encoder position and button state
    updateEncoderPos();
    encoderButton.poll();

    static bool lastButtonState = false;
    bool currentButtonState = encoderButton.pushed();

    // Toggle Nixie tubes on/off with long press (3 seconds)
    if (encoderButton.longPress()) {
        manualOverride = true;
        nixieOn = !nixieOn;
        menu = TOP;
        updateSelection();
    } else if (currentButtonState && !lastButtonState) {
        menuTimer = now();
        updateMenu();
    }
    lastButtonState = currentButtonState;

    // Update NeoPixel LEDs
    updateLEDs();

    // Sync time in TOP menu
    if (menu == TOP) {
        timeClient.update();
        setTime(myTZ.toLocal(timeClient.getEpochTime()));
    }

    // Check WiFi and update time every 2 hours
    static unsigned long lastNtpUpdate = 0;
    const unsigned long ntpUpdateInterval = 7200000; // 2 hours
    if (millis() - lastNtpUpdate >= ntpUpdateInterval) {
        if (WiFi.status() == WL_CONNECTED) {
            if (!wifiConnected) wifiConnected = true;
            if (timeClient.update()) setTime(myTZ.toLocal(timeClient.getEpochTime()));
        } else if (wifiConnected) {
            wifiConnected = false;
        }
        lastNtpUpdate = millis();
    }

    // Handle cathode protection
    static time_t lastTriggerMinute = -1;
    if (interval_indx > 0 && interval_indx < num_intervals) {
        if (menu != SET_UTC_OFFSET && menu != ENABLE_DST) {
            int currentMinute = minute();
            int intervalMinutes = intervals[interval_indx];
            if (currentMinute % intervalMinutes == 0 && currentMinute != lastTriggerMinute && second() == 0) {
                if (nixieOn && !nixie.isSlotMachineActive()) {
                    cathodeProtect();
                    lastTriggerMinute = currentMinute;
                }
            }
        }
    }

    // Update slot machine effect every 50ms
    static unsigned long lastUpdate = 0;
    const unsigned long updateInterval = 50;
    if (millis() - lastUpdate >= updateInterval) {
        nixie.updateSlotMachine();
        lastUpdate = millis();
    }

    // Update display based on current state
    static time_t prevTime = 0;
    static time_t refreshScreensaver = 0;
    if (millis() != prevTime) {
        prevTime = millis();
        evalShutoffTime();
        if (!nixie.isSlotMachineActive()) {
            if (second() >= showdate_second && second() < (showdate_second + showdate_duration) && showDate && (minute() % showdate_interval == 0)) {
                displayDate();
            } else {
                displayTime();
            }
        }
    }

    // Handle screensaver
    if (menu == SCREENSAVER) {
        if (now() - refreshScreensaver > 1800) { // Refresh every 30 minutes to prevent corruption
            updateSelection();
            refreshScreensaver = now();
        } else if (ssOption == 1 && second() % 2 == 0) { // Scroll every 2 seconds
            if (millis() % 1000 == 0) {
                long r = random(2);
                if (r == 0) display.startscrolldiagright(0x00, 0x07);
                else display.startscrolldiagleft(0x00, 0x07);
            }
        }
    }

    // Reset to screensaver or TOP menu after 60 seconds of inactivity
    static Menu lastMenu = TOP;
    static time_t lastMenuChange = now();
    if (menu != lastMenu) {
        lastMenuChange = now();
        lastMenu = menu;
    }
    if (now() - menuTimer > 60 && menu != SCREENSAVER && menu != TOP) {
        if (ssOption > 0) {
            menu = SCREENSAVER;
            updateSelection();
            refreshScreensaver = now();
        } else if (menu != TOP) {
            menu = TOP;
            updateSelection();
        }
    }
}

/**
 * Trigger cathode protection effect
 */
void cathodeProtect() {
    int startHour = set12_24 ? hour() : hourFormat12();
    int startMinute = minute();
    int startSecond = second();

    time_t currentTime = myTZ.toLocal(timeClient.getEpochTime());
    time_t endTime = currentTime + 5; // Target time is 5 seconds ahead
    int targetHour = set12_24 ? hour(endTime) : hourFormat12(endTime);
    int targetMinute = minute(endTime);
    int targetSecond = second(endTime);

    nixie.startSlotMachine(startHour, startMinute, startSecond, targetHour, targetMinute, targetSecond);
}

/**
 * Display current time on Nixie tubes and OLED
 */
void displayTime() {
    char tod[10], time_str[20], date_str[20];
    const char* am_pm[] = {"AM", "PM"};
    const char* month_names[] = {"Jan", "Feb", "March", "April", "May", "June", "July", "Aug", "Sept", "Oct", "Nov", "Dec"};
    int hour12_24 = set12_24 ? (unsigned char)hour() : (unsigned char)hourFormat12();
    unsigned char hourBcd = decToBcd((unsigned char)hour12_24);
    unsigned char minBcd = decToBcd((unsigned char)minute());
    bool colonBlinkState;

    // Handle colon blinking
    static unsigned long lastSecondChange = 0;
    static int lastSecond = -1;
    int currentSecond = second();
    unsigned long currentMillis = millis();

    if (currentSecond != lastSecond) {
        lastSecondChange = currentMillis;
        lastSecond = currentSecond;
    }

    switch (enableBlink) {
        case 0: colonBlinkState = false; break;
        case 1: colonBlinkState = !(bool)(currentSecond % 2); break;
        case 2: colonBlinkState = (currentMillis - lastSecondChange) < 500; break;
        case 3: colonBlinkState = true; break;
    }

    // Control high voltage supply
    hv_supply.switchOn();

    static bool lastNixieState = true;
    if (!nixieOn) {
        hourBcd = 255;
        minBcd = 255;
        hv_supply.switchOff();
        LED_effect = 0;
        colonBlinkState = false;
    } else if (lastNixieState == false) {
        LED_effect = EEPROM.read(EEPROM_addr_led);
        if (LED_effect > 14) LED_effect = 0;
    }
    lastNixieState = nixieOn;

    // Update Nixie tubes with animation
    if (transitionFromDate == false) {
        taskBegin();
        nixie.disableSegments(hourTens, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(hourUnits, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(minuteTens, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(minuteUnits, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(secondTens, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(secondUnits, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);

        if (!showZero && hour12_24 < 10) {
            nixie.disableSegments(hourTens, 10);
        } else {
            nixie.enableSegment(hourTens[(hour12_24 / 10) % 10]);
        }
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.enableSegment(hourUnits[hour12_24 % 10]);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.enableSegment(minuteTens[(minute() / 10) % 10]);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.enableSegment(minuteUnits[minute() % 10]);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.enableSegment(secondTens[(second() / 10) % 10]);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.enableSegment(secondUnits[second() % 10]);
        nixie.updateDisplay();
        taskDelay(waitTime);

        transitionFromDate = true;
        transitionToDate = false;
        taskEnd();
    } else {
        nixie.disableAllSegments();
        if (!showZero && hour12_24 < 10) {
            nixie.disableSegments(hourTens, 10);
        } else {
            nixie.enableSegment(hourTens[(hour12_24 / 10) % 10]);
        }
        nixie.enableSegment(hourUnits[hour12_24 % 10]);
        nixie.enableSegment(minuteTens[(minute() / 10) % 10]);
        nixie.enableSegment(minuteUnits[minute() % 10]);
        nixie.enableSegment(secondTens[(second() / 10) % 10]);
        nixie.enableSegment(secondUnits[second() % 10]);
        transitionToDate = false;
    }

    // Update colon dots
    if (!colonBlinkState || enableBlink == 0) {
        nixie.disableSegment(UpperLeftDot);
        nixie.disableSegment(LowerLeftDot);
        nixie.disableSegment(UpperRightDot);
        nixie.disableSegment(LowerRightDot);
    } else {
        nixie.enableSegment(UpperLeftDot);
        nixie.enableSegment(LowerLeftDot);
        nixie.enableSegment(UpperRightDot);
        nixie.enableSegment(LowerRightDot);
    }

    nixie.updateDisplay();

    // Update OLED display
    if (menu == TOP || menu == SET_UTC_OFFSET) {
        if (menu == TOP) {
            display.drawBitmap(20, 0, NixieN_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
            display.drawBitmap(38, 0, NixieI_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
            display.drawBitmap(56, 0, NixieX_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
            display.drawBitmap(74, 0, NixieI_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
            display.drawBitmap(92, 0, NixieE_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
        }

        formattedTime(tod, hour12_24, minute(), second());
        sprintf(time_str, "%s %s", tod, am_pm[isPM()]);
        sprintf(date_str, "%s %d, %d", month_names[month() - 1], day(), year());
        display.fillRect(20, 28, 120, 8, BLACK);
        display.setCursor(20, 28);
        display.print(time_str);
        if (enableDST) {
            if (myTZ.utcIsDST(myTZ.toUTC(now()))) {
                display.print(" DST");
            } else {
                display.print(" STD");
            }
        }
        display.setCursor(20, 36);
        display.print(date_str);
        display.display();
    }
}

/**
 * Display current date on Nixie tubes and OLED
 */
void displayDate() {
    char tod[10], time_str[20], date_str[20];
    const char* am_pm[] = {"AM", "PM"};
    const char* month_names[] = {"Jan", "Feb", "March", "April", "May", "June", "July", "Aug", "Sept", "Oct", "Nov", "Dec"};
    int hour12_24 = set12_24 ? (unsigned char)hour() : (unsigned char)hourFormat12();
    unsigned char hourBcd = decToBcd((unsigned char)hour12_24);
    unsigned char minBcd = decToBcd((unsigned char)minute());

    hv_supply.switchOn();

    static bool lastNixieState = true;
    if (!nixieOn) {
        hourBcd = 255;
        minBcd = 255;
        hv_supply.switchOff();
        LED_effect = 0;
    } else if (lastNixieState == false) {
        LED_effect = EEPROM.read(EEPROM_addr_led);
        if (LED_effect > 14) LED_effect = 0;
    }
    lastNixieState = nixieOn;

    if (transitionToDate == false) {
        taskBegin();
        nixie.disableSegments(hourTens, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(hourUnits, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(minuteTens, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(minuteUnits, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(secondTens, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);
        nixie.disableSegments(secondUnits, 10);
        nixie.updateDisplay();
        taskDelay(waitTime);

        if (dateFormat == 0) { // DD:MM:YY
            nixie.enableSegment(hourTens[(day() / 10) % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(hourUnits[day() % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(minuteTens[(month() / 10) % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(minuteUnits[month() % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(secondTens[(year() / 10) % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(secondUnits[year() % 10]);
        } else if (dateFormat == 1) { // MM:DD:YY
            nixie.enableSegment(hourTens[(month() / 10) % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(hourUnits[month() % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(minuteTens[(day() / 10) % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(minuteUnits[day() % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(secondTens[(year() / 10) % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(secondUnits[year() % 10]);
        } else if (dateFormat == 2) { // YY:MM:DD
            nixie.enableSegment(hourTens[(year() / 10) % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(hourUnits[year() % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(minuteTens[(month() / 10) % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(minuteUnits[month() % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(secondTens[(day() / 10) % 10]);
            nixie.updateDisplay();
            taskDelay(waitTime);
            nixie.enableSegment(secondUnits[day() % 10]);
        }
        transitionToDate = true;
        transitionFromDate = false;
        taskEnd();
    } else {
        nixie.disableAllSegments();
        if (dateFormat == 0) { // DD:MM:YY
            nixie.enableSegment(hourTens[(day() / 10) % 10]);
            nixie.enableSegment(hourUnits[day() % 10]);
            nixie.enableSegment(minuteTens[(month() / 10) % 10]);
            nixie.enableSegment(minuteUnits[month() % 10]);
            nixie.enableSegment(secondTens[(year() / 10) % 10]);
            nixie.enableSegment(secondUnits[year() % 10]);
        } else if (dateFormat == 1) { // MM:DD:YY
            nixie.enableSegment(hourTens[(month() / 10) % 10]);
            nixie.enableSegment(hourUnits[month() % 10]);
            nixie.enableSegment(minuteTens[(day() / 10) % 10]);
            nixie.enableSegment(minuteUnits[day() % 10]);
            nixie.enableSegment(secondTens[(year() / 10) % 10]);
            nixie.enableSegment(secondUnits[year() % 10]);
        } else if (dateFormat == 2) { // YY:MM:DD
            nixie.enableSegment(hourTens[(year() / 10) % 10]);
            nixie.enableSegment(hourUnits[year() % 10]);
            nixie.enableSegment(minuteTens[(month() / 10) % 10]);
            nixie.enableSegment(minuteUnits[month() % 10]);
            nixie.enableSegment(secondTens[(day() / 10) % 10]);
            nixie.enableSegment(secondUnits[day() % 10]);
        }
        transitionFromDate = false;
    }

    // Update colon dots for date display
    nixie.disableSegment(UpperLeftDot);
    nixie.enableSegment(LowerLeftDot);
    nixie.disableSegment(UpperRightDot);
    nixie.enableSegment(LowerRightDot);

    nixie.updateDisplay();

    // Update OLED display
    if (menu == TOP || menu == SET_UTC_OFFSET) {
        if (menu == TOP) {
            display.drawBitmap(20, 0, NixieN_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
            display.drawBitmap(38, 0, NixieI_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
            display.drawBitmap(56, 0, NixieX_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
            display.drawBitmap(74, 0, NixieI_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
            display.drawBitmap(92, 0, NixieE_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
        }

        formattedTime(tod, hour12_24, minute(), second());
        sprintf(time_str, "%s %s", tod, am_pm[isPM()]);
        sprintf(date_str, "%s %d, %d", month_names[month() - 1], day(), year());
        display.fillRect(20, 28, 120, 8, BLACK);
        display.setCursor(20, 28);
        display.print(time_str);
        if (enableDST) {
            if (myTZ.utcIsDST(myTZ.toUTC(now()))) {
                display.print(" DST");
            } else {
                display.print(" STD");
            }
        }
        display.setCursor(20, 36);
        display.print(date_str);
        display.display();
    }
}

/**
 * Convert decimal value to BCD (Binary-Coded Decimal)
 */
unsigned char decToBcd(unsigned char val) {
    return (((val / 10) * 16) + (val % 10));
}

/**
 * Format time string with colons
 */
#define colonDigit(digit) digit < 10 ? ":0" : ":"
void formattedTime(char* tod, int hours, int minutes, int seconds) {
    sprintf(tod, "%d%s%d%s%d", hours, colonDigit(minutes), minutes, colonDigit(seconds), seconds);
}

/**
 * Evaluate whether Nixie tubes should be turned off based on auto shutoff settings
 */
void evalShutoffTime() {
    if (!enableAutoShutoff) return;

    int mn = 60 * hour() + minute();
    int mn_on = 15 * autoShutoffOntime;
    int mn_off = 15 * autoShutoffOfftime;

    static bool prevShutoffState = true;
    if (((mn_off < mn_on) && (mn >= mn_off) && (mn < mn_on)) ||
        (mn_off > mn_on) && ((mn >= mn_off) || (mn < mn_on))) {
        if (!manualOverride) nixieOn = false;
        if (prevShutoffState == true) manualOverride = false;
        prevShutoffState = false;
    } else {
        if (!manualOverride) nixieOn = true;
        if (prevShutoffState == false) manualOverride = false;
        prevShutoffState = true;
    }
}

/**
 * Update encoder position based on rotation
 */
void updateEncoderPos() {
    static int encoderA, encoderB, encoderA_prev;

    encoderA = digitalRead(encoderPinA);
    encoderB = digitalRead(encoderPinB);

    if ((!encoderA) && (encoderA_prev)) { // A has gone from high to low
        encoderPosPrev = encoderPos;
        encoderB ? encoderPos++ : encoderPos--;
        menuTimer = now();
        if (menu != TOP) updateSelection();
    }
    encoderA_prev = encoderA;
}

#ifdef CLOCK_COLON
const int n_set1 = 6; // 6 menu items in SETTINGS1 with colon
#else
const int n_set1 = 5; // 5 menu items without colon
#endif

/**
 * Handle menu navigation when button is pressed
 */
void updateMenu() {
    switch (menu) {
        case TOP:
            menu = SETTINGS1;
            break;

        case SCREENSAVER:
            display.stopscroll();
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = SETTINGS1;
            break;

        case SETTINGS1:
            switch (mod(encoderPos, n_set1)) {
                case 0:
                    menu = SET_UTC_OFFSET;
                    break;
                case 1:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = DST_MENU;
                    break;
                case 2:
                    menu = SET_12_24;
                    break;
#ifdef CLOCK_COLON
                case 3:
                    menu = BLINK_COLON;
                    break;
                case 4:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS2;
                    break;
                case 5:
                    menu = TOP;
                    break;
#else
                case 3:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS2;
                    break;
                case 4:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = TOP;
                    break;
#endif
            }
            break;

        case SETTINGS2:
            switch (mod(encoderPos, 6)) {
                case 0:
                    menu = CATHODE_PROTECT;
                    break;
                case 1:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = AUTO_SHUTOFF;
                    break;
                case 2:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = LED_MENU;
                    break;
                case 3:
                    menu = SHOW_ZERO;
                    break;
                case 4:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS3;
                    break;
                case 5:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS1;
                    break;
            }
            break;

        case SETTINGS3:
            switch (mod(encoderPos, 5)) {
                case 0:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SHOW_DATE_MENU;
                    break;
                case 1:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SCREENSAVER_MENU;
                    break;
                case 2:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = RESET_WIFI;
                    break;
                case 3:
                    menu = SET_TIME;
                    field = 0;
                    encoderButton.poll();
                    break;
                case 4:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS2;
                    break;
            }
            break;

        case DST_MENU:
            switch (mod(encoderPos, 3)) {
                case 0:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = ENABLE_DST;
                    break;
                case 1:
                    menu = DST_RULE;
                    break;
                case 2:
                    encoderPos = 1;
                    encoderPosPrev = 1;
                    menu = SETTINGS1;
                    break;
            }
            break;

        case RESET_WIFI:
            switch (mod(encoderPos, 2)) {
                case 0:
                    encoderPos = 2;
                    encoderPosPrev = 2;
                    menu = SETTINGS3;
                    break;
                case 1:
                    resetWiFi();
                    menu = TOP;
                    break;
            }
            break;

        case SET_UTC_OFFSET: {
            EEPROM.write(EEPROM_addr_UTC_offset, (unsigned char)(mod(mySTD.offset / 60, 24)));
            EEPROM.commit();
            initProtectionTimer = false;
            time_t currentTime = timeClient.getEpochTime();
            time_t localTime = myTZ.toLocal(currentTime);
            setTime(localTime);
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = SETTINGS1;
            firstEntry = true;
            break;
        }

        case ENABLE_DST:
            EEPROM.write(EEPROM_addr_DST, (unsigned char)enableDST);
            EEPROM.commit();
            initProtectionTimer = false;
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = DST_MENU;
            break;

        case DST_RULE:
            EEPROM.write(EEPROM_addr_DST_rule, currentTimeZone);
            EEPROM.commit();
            myDST = dstRules[currentTimeZone];
            mySTD = stdRules[currentTimeZone];
            myDST.offset = mySTD.offset;
            if (enableDST) myDST.offset += 60;
            myTZ = Timezone(myDST, mySTD);
            initProtectionTimer = false;
            encoderPos = 1;
            encoderPosPrev = 1;
            menu = DST_MENU;
            break;

        case SET_12_24:
            EEPROM.write(EEPROM_addr_12_24, (unsigned char)set12_24);
            EEPROM.commit();
            encoderPos = 2;
            encoderPosPrev = 2;
            menu = SETTINGS1;
            break;

        case BLINK_COLON:
            EEPROM.write(EEPROM_addr_blink_colon, (unsigned char)enableBlink);
            EEPROM.commit();
            encoderPos = 3;
            encoderPosPrev = 3;
            menu = SETTINGS1;
            break;

        case CATHODE_PROTECT:
            EEPROM.write(EEPROM_addr_protect, interval_indx);
            EEPROM.commit();
            initProtectionTimer = false;
            protectTimer = 0;
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = SETTINGS2;
            break;

        case AUTO_SHUTOFF:
            switch (mod(encoderPos, 4)) {
                case 0:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = AUTO_SHUTOFF_ENABLE;
                    break;
                case 1:
                    menu = AUTO_SHUTOFF_OFFTIME;
                    break;
                case 2:
                    menu = AUTO_SHUTOFF_ONTIME;
                    break;
                case 3:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS2;
                    break;
            }
            break;

        case AUTO_SHUTOFF_ENABLE:
            EEPROM.write(EEPROM_addr_shutoff_en, (unsigned char)enableAutoShutoff);
            EEPROM.commit();
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = AUTO_SHUTOFF;
            break;

        case AUTO_SHUTOFF_OFFTIME:
            EEPROM.write(EEPROM_addr_shutoff_off, (unsigned char)autoShutoffOfftime);
            EEPROM.commit();
            encoderPos = 1;
            encoderPosPrev = 1;
            menu = AUTO_SHUTOFF;
            break;

        case AUTO_SHUTOFF_ONTIME:
            EEPROM.write(EEPROM_addr_shutoff_on, (unsigned char)autoShutoffOntime);
            EEPROM.commit();
            encoderPos = 2;
            encoderPosPrev = 2;
            menu = AUTO_SHUTOFF;
            break;

        case SCREENSAVER_MENU: {
            int opt = mod(encoderPos, 4);
            if (opt < 3) {
                ssOption = (uint8_t)opt;
                EEPROM.write(EEPROM_addr_screensaver, (unsigned char)ssOption);
                EEPROM.commit();
            } else {
                encoderPos = 1;
                encoderPosPrev = 1;
                menu = SETTINGS3;
            }
            break;
        }

        case LED_MENU: {
            int opt = mod(encoderPos, 6);
            if (opt < 3) {
                LED_effect = (uint8_t)opt;
                static_color = 2; // Reset to default static color
                EEPROM.write(EEPROM_addr_led, LED_effect);
                EEPROM.write(EEPROM_addr_static_color, static_color);
                EEPROM.commit();
                updateLEDs();
            } else {
                encoderPos = 2;
                encoderPosPrev = 2;
                menu = SETTINGS2;
            }
            if (opt == 3) menu = STATIC_COLOR;
            if (opt == 4) menu = LED_BRIGHTNESS;
            break;
        }

        case STATIC_COLOR:
            EEPROM.write(EEPROM_addr_led, LED_effect);
            EEPROM.write(EEPROM_addr_static_color, static_color_indx);
            EEPROM.commit();
            encoderPos = 3;
            encoderPosPrev = 3;
            menu = LED_MENU;
            break;

        case LED_BRIGHTNESS:
            EEPROM.write(EEPROM_addr_led_brightness, (unsigned char)led_brightness_indx);
            EEPROM.commit();
            encoderPos = 4;
            encoderPosPrev = 4;
            menu = LED_MENU;
            break;

        case SHOW_ZERO:
            EEPROM.write(EEPROM_addr_showzero, (unsigned char)showZero);
            EEPROM.commit();
            encoderPos = 3;
            encoderPosPrev = 3;
            menu = SETTINGS2;
            break;

        case SHOW_DATE_MENU:
            switch (mod(encoderPos, 6)) {
                case 0:
                    menu = SHOW_DATE;
                    break;
                case 1:
                    menu = SHOW_DATE_SECOND;
                    break;
                case 2:
                    menu = SHOW_DATE_DURATION;
                    break;
                case 3:
                    menu = SHOW_DATE_INTERVAL;
                    break;
                case 4:
                    menu = SHOW_DATE_FORMAT;
                    break;
                case 5:
                    encoderPos = 0;
                    encoderPosPrev = 0;
                    menu = SETTINGS3;
                    break;
            }
            break;

        case SHOW_DATE:
            EEPROM.write(EEPROM_addr_showdate, (unsigned char)showDate);
            EEPROM.commit();
            encoderPos = 0;
            encoderPosPrev = 0;
            menu = SHOW_DATE_MENU;
            break;

        case SHOW_DATE_SECOND:
            EEPROM.write(EEPROM_addr_showdate_second, (unsigned char)showdate_second);
            EEPROM.commit();
            encoderPos = 1;
            encoderPosPrev = 1;
            menu = SHOW_DATE_MENU;
            break;

        case SHOW_DATE_INTERVAL:
            EEPROM.write(EEPROM_addr_showdate_interval, showdate_interval);
            EEPROM.commit();
            encoderPos = 3;
            encoderPosPrev = 3;
            menu = SHOW_DATE_MENU;
            break;

        case SHOW_DATE_DURATION:
            EEPROM.write(EEPROM_addr_showdate_duration, showdate_duration);
            EEPROM.commit();
            encoderPos = 2;
            encoderPosPrev = 2;
            menu = SHOW_DATE_MENU;
            break;

        case SHOW_DATE_FORMAT:
            EEPROM.write(EEPROM_addr_date_format, dateFormat);
            EEPROM.commit();
            encoderPos = 4;
            encoderPosPrev = 4;
            menu = SHOW_DATE_MENU;
            break;

        case SET_TIME:
            switch (mod(encoderPos, 3)) {
                case 0:
                    menu = SET_TIME_MANUALLY;
                    break;
                case 1:
                    menu = SET_TIME_WIFI;
                    break;
                case 2:
                    encoderPos = 3;
                    encoderPosPrev = 3;
                    menu = SETTINGS3;
                    break;
            }
            break;

        case SET_TIME_WIFI: {
            timeClient.update();
            time_t currentTime = timeClient.getEpochTime();
            time_t localTime = myTZ.toLocal(currentTime);
            setTime(localTime);
            setHour = hour(localTime);
            setMinute = minute(localTime);
            setDay = day(localTime);
            setMonth = month(localTime);
            setYear = year(localTime);
            setAmPm = isPM(localTime) ? 1 : 0;
            encoderPos = 1;
            encoderPosPrev = 1;
            menu = SET_TIME;
            break;
        }

        case SET_TIME_MANUALLY:
            // Handled separately in updateSelection() to avoid jump issues
            break;
    }
    updateSelection();
}

/**
 * Update OLED display based on current menu selection
 */
void updateSelection() {
    // Declare variables outside switch to avoid jump issues
    int UTC_STD_Offset = 0;
    int dispOffset = 0;
    char timestr[7]; // Buffer for time strings in AUTO_SHUTOFF

    if (menu != SET_UTC_OFFSET) {
        display.clearDisplay();
    }

    switch (menu) {
        case TOP:
            display.setTextColor(WHITE, BLACK);
            display.setCursor(0, 56);
            display.print("Click for settings");
            break;

        case SCREENSAVER:
            if (ssOption == 1) { // Scrolling image screensaver
                display.clearDisplay();
                int scrollPos = (now() % 20) - 10; // Simple scroll effect
                display.drawBitmap(20 + scrollPos, 13, NixieN_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
                display.drawBitmap(38 + scrollPos, 13, NixieI_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
                display.drawBitmap(56 + scrollPos, 13, NixieX_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
                display.drawBitmap(74 + scrollPos, 13, NixieI_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
                display.drawBitmap(92 + scrollPos, 13, NixieE_bmp, NIXIE_WIDTH, NIXIE_HEIGHT, WHITE);
                display.setCursor(10, 42);
                display.print("Click for settings");
                display.display();
            }
            break;

        case ENABLE_DST:
            if (encoderPos != encoderPosPrev) enableDST = !enableDST;
            myDST = dstRules[currentTimeZone];
            mySTD = stdRules[currentTimeZone];
            myDST.offset = mySTD.offset;
            if (enableDST) myDST.offset += 60;
            myTZ = Timezone(myDST, mySTD);
            // Fall through to DST_MENU

        case DST_MENU:
            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.print("DST SETTINGS");
            display.setCursor(0, 16);

            if (menu == DST_MENU) setHighlight(0, 3);
            else display.setTextColor(WHITE, BLACK);
            display.print("Auto DST");
            display.setTextColor(WHITE, BLACK);
            display.print("   ");
            if (menu == ENABLE_DST) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.println(enableDST ? "On" : "Off");

            if (menu == DST_MENU) setHighlight(1, 3);
            else display.setTextColor(WHITE, BLACK);
            display.print("DST Rule");
            display.setTextColor(WHITE, BLACK);
            display.print("   ");
            if (menu == DST_RULE) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            switch (currentTimeZone) {
                case TZ_EUROPE: display.println("Europe"); break;
                case TZ_USA: display.println("USA"); break;
                case TZ_AUSTRALIA: display.println("Aus"); break;
                case TZ_NEWZEALAND: display.println("NZ"); break;
                case TZ_CHILE: display.println("Chile"); break;
            }

            if (menu == DST_MENU) setHighlight(2, 3);
            else display.setTextColor(WHITE, BLACK);
            display.println("Return");
            break;

        case DST_RULE:
            if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = currentTimeZone;
            currentTimeZone = mod(encoderPos, 5);
            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.println("SELECT DST RULE");
            display.setCursor(0, 16);
            display.print("Timezone: ");
            if (menu == DST_RULE) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            switch (currentTimeZone) {
                case TZ_EUROPE: display.println("Europe"); break;
                case TZ_USA: display.println("USA"); break;
                case TZ_AUSTRALIA: display.println("Australia"); break;
                case TZ_NEWZEALAND: display.println("New Zealand"); break;
                case TZ_CHILE: display.println("Chile"); break;
            }
            display.setTextColor(WHITE, BLACK);
            display.setCursor(0, 48);
            display.println("Press knob to");
            display.print("confirm DST Rule");
            break;

        case SET_12_24:
            if (menu == SET_12_24 && encoderPos != encoderPosPrev) set12_24 = !set12_24;
            displayTime();
            // Fall through to SETTINGS1

        case BLINK_COLON:
            if (menu == BLINK_COLON) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = enableBlink_indx;
                enableBlink_indx = mod(encoderPos, enableBlink_num_state);
                enableBlink = enableBlink_state[enableBlink_indx];
            }
            // Fall through to SETTINGS1

        case SETTINGS1:
            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.print("SETTINGS (1 of 3)");
            display.setCursor(0, 16);

            if (menu == SETTINGS1) setHighlight(0, n_set1);
            display.print("Set UTC Offset");
            display.setTextColor(WHITE, BLACK);
            display.println("  ");

            if (menu == SETTINGS1) setHighlight(1, n_set1);
            display.print("Auto DST");
            display.setTextColor(WHITE, BLACK);
            display.print("        ");
            display.println(enableDST ? "On" : "Off");

            if (menu == SETTINGS1) setHighlight(2, n_set1);
            else display.setTextColor(WHITE, BLACK);
            display.print("12/24 Hours");
            display.setTextColor(WHITE, BLACK);
            display.print("     ");
            if (menu == SET_12_24) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.println(set12_24 ? "24" : "12");

#ifdef CLOCK_COLON
            if (menu == SETTINGS1) setHighlight(3, n_set1);
            else display.setTextColor(WHITE, BLACK);
            display.print("Colon");
            display.setTextColor(WHITE, BLACK);
            display.print("           ");
            if (menu == BLINK_COLON) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            switch (enableBlink) {
                case 0: display.println("off"); break;
                case 1: display.println("slow"); break;
                case 2: display.println("fast"); break;
                case 3: display.println("on"); break;
            }
#endif

            if (menu == SETTINGS1) setHighlight(n_set1 - 2, n_set1);
            else display.setTextColor(WHITE, BLACK);
            display.println("More Options");

            if (menu == SETTINGS1) setHighlight(n_set1 - 1, n_set1);
            display.println("Return");
            break;

        case CATHODE_PROTECT:
            if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = interval_indx;
            interval_indx = mod(encoderPos, num_intervals);
            // Fall through to SETTINGS2

        case SHOW_ZERO:
            if (menu == SHOW_ZERO && encoderPos != encoderPosPrev) {
                showZero = !showZero;
                displayTime();
            }
            // Fall through to SETTINGS2

        case SETTINGS2:
            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.print("SETTINGS (2 of 3)");
            display.setCursor(0, 16);

            if (menu == SETTINGS2) setHighlight(0, 6);
            display.print("Protect Cathode");
            display.setTextColor(WHITE, BLACK);
            display.print(" ");
            if (menu == CATHODE_PROTECT) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            if (interval_indx == 0) display.println("Off");
            else display.println(intervals[interval_indx]);

            if (menu == SETTINGS2) setHighlight(1, 6);
            else display.setTextColor(WHITE, BLACK);
            display.print("Auto Shut Off");
            display.setTextColor(WHITE, BLACK);
            display.print("   ");
            display.setTextColor(WHITE, BLACK);
            display.println(enableAutoShutoff ? "On " : "Off");

            if (menu == SETTINGS2) setHighlight(2, 6);
            else display.setTextColor(WHITE, BLACK);
            display.print("LED Effect");
            display.setTextColor(WHITE, BLACK);
            display.print("      ");
            display.setTextColor(WHITE, BLACK);
            display.println((LED_effect > 0) ? "On " : "Off");

            if (menu == SETTINGS2) setHighlight(3, 6);
            else display.setTextColor(WHITE, BLACK);
            display.print("Show Zero");
            display.setTextColor(WHITE, BLACK);
            display.print("       ");
            if (menu == SHOW_ZERO) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.println(showZero ? "On " : "Off");

            if (menu == SETTINGS2) setHighlight(4, 6);
            else display.setTextColor(WHITE, BLACK);
            display.println("More Options");

            if (menu == SETTINGS2) setHighlight(5, 6);
            display.println("Return");
            break;

        case SETTINGS3:
            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.print("SETTINGS (3 of 3)");
            display.setCursor(0, 16);

            if (menu == SETTINGS3) setHighlight(0, 5);
            else display.setTextColor(WHITE, BLACK);
            display.print("Show Date");
            display.setTextColor(WHITE, BLACK);
            display.print("       ");
            if (menu == SHOW_DATE) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.println(showDate ? "On " : "Off");

            if (menu == SETTINGS3) setHighlight(1, 5);
            else display.setTextColor(WHITE, BLACK);
            display.print("Screensaver");
            display.setTextColor(WHITE, BLACK);
            display.print("     ");
            display.setTextColor(WHITE, BLACK);
            display.println((ssOption > 0) ? "On " : "Off");

            if (menu == SETTINGS3) setHighlight(2, 5);
            else display.setTextColor(WHITE, BLACK);
            display.print("Reset Wifi");
            display.setTextColor(WHITE, BLACK);
            display.println("      ");

            if (menu == SETTINGS3) setHighlight(3, 5);
            else display.setTextColor(WHITE, BLACK);
            display.print("Set Time/Date");
            display.setTextColor(WHITE, BLACK);
            display.println("   ");

            if (menu == SETTINGS3) setHighlight(4, 5);
            display.println("Return");
            break;

        case SET_TIME:
            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.println("SET DATE/TIME");
            display.setCursor(0, 16);

            if (menu == SET_TIME) setHighlight(0, 3);
            display.print("Set manually");
            display.setTextColor(WHITE, BLACK);
            display.println("    ");

            if (menu == SET_TIME) setHighlight(1, 3);
            display.print("WiFi sync");
            display.setTextColor(WHITE, BLACK);
            display.println("       ");

            if (menu == SET_TIME) setHighlight(2, 3);
            display.println("Return");
            break;

        case SET_TIME_MANUALLY: {
            static bool firstRun = true;
            const int maxFields = set12_24 ? 5 : 6; // Fields without confirmation
            static unsigned long lastPressTime = 0;
            const unsigned long debounceDelay = 200;

            if (!firstRun && encoderButton.pushed() && (millis() - lastPressTime > debounceDelay)) {
                lastPressTime = millis();
                field++;
                if (field > maxFields + 1) field = maxFields + 1;
            }
            firstRun = false;

            if (encoderPos != encoderPosPrev && field <= maxFields) {
                switch (field) {
                    case 0: // Hour
                        if (set12_24) setHour = mod(encoderPos, 24);
                        else setHour = constrain(mod(encoderPos, 12) + 1, 1, 12);
                        break;
                    case 1: // Minute
                        setMinute = mod(encoderPos, 60);
                        break;
                    case 2: // Day
                        setDay = constrain(mod(encoderPos, 32), 1, 31);
                        break;
                    case 3: // Month
                        setMonth = constrain(mod(encoderPos, 13), 1, 12);
                        break;
                    case 4: // Year
                        setYear = constrain(2000 + encoderPos, 2000, 2099);
                        break;
                    case 5: // AM/PM (only in 12-hour mode)
                        if (!set12_24) setAmPm = mod(encoderPos, 2);
                        break;
                }
                encoderPosPrev = encoderPos;
            }

            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.println("SET TIME/DATE");
            display.setCursor(0, 16);
            display.print("Time: ");
            if (field == 0) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.print(setHour < 10 ? "0" : "");
            display.print(setHour);
            display.setTextColor(WHITE, BLACK);
            display.print(":");
            if (field == 1) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.print(setMinute < 10 ? "0" : "");
            display.print(setMinute);
            if (!set12_24) {
                display.print(" ");
                if (field == 5) display.setTextColor(BLACK, WHITE);
                else display.setTextColor(WHITE, BLACK);
                display.println(amPmLabels[setAmPm]);
            } else {
                display.println();
            }
            display.setTextColor(WHITE, BLACK);
            display.println();
            display.println("Date (DD:MM:YYYY): ");
            if (field == 2) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.print(setDay < 10 ? "0" : "");
            display.print(setDay);
            display.setTextColor(WHITE, BLACK);
            display.print(".");
            if (field == 3) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.print(setMonth < 10 ? "0" : "");
            display.print(setMonth);
            display.setTextColor(WHITE, BLACK);
            display.print(".");
            if (field == 4) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.println(setYear);
            display.setTextColor(WHITE, BLACK);
            display.setCursor(0, 56);

            if (field < maxFields) {
                display.println("Press to next");
            } else if (field == maxFields) {
                display.println("Press to confirm");
            } else { // field == maxFields + 1
                display.println("Saving...");
                display.display();
                if (set12_24) {
                    setTime(setHour, setMinute, 0, setDay, setMonth, setYear);
                } else {
                    int adjustedHour = setHour + (setAmPm ? 12 : 0);
                    if (setHour == 12) adjustedHour = setAmPm ? 12 : 0;
                    setTime(adjustedHour, setMinute, 0, setDay, setMonth, setYear);
                }
                delay(500);
                menu = SET_TIME;
                field = 0;
                firstRun = true;
                menuTimer = now();
                encoderPos = 0;
                encoderPosPrev = 0;
                display.clearDisplay();
                updateSelection();
                return;
            }
            break;
        }

        case SET_TIME_WIFI:
            {
            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.println("SYNC VIA WIFI");
            display.setCursor(0, 16);
            display.print("Syncing...");
            display.display();
            timeClient.update();
            time_t currentTime = timeClient.getEpochTime();
            time_t localTime = myTZ.toLocal(currentTime);
            setTime(localTime);
            setHour = hour(localTime);
            setMinute = minute(localTime);
            setDay = day(localTime);
            setMonth = month(localTime);
            setYear = year(localTime);
            setAmPm = isPM(localTime) ? 1 : 0;
            display.clearDisplay();
            display.setCursor(0, 0);
            display.println("SYNC VIA WIFI");
            display.setCursor(0, 16);
            display.println("Time synced");
            display.display();
            delay(1000);
            menu = SET_TIME;
            menuTimer = now();
            encoderPos = 1;
            encoderPosPrev = 1;
            updateSelection();
            }
            break;

        case RESET_WIFI:
            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.print("RESET WIFI?");
            display.setCursor(0, 16);
            setHighlight(0, 2);
            display.println("No");
            setHighlight(1, 2);
            display.println("Yes");
            break;

        case SET_UTC_OFFSET:
            {
            UTC_STD_Offset = mySTD.offset / 60;

            if (firstEntry) {
                display.clearDisplay();
                display.setCursor(0, 0);
                display.setTextColor(WHITE, BLACK);
                display.println("SET TIMEZONE OFFSET");
                display.println();
                display.setCursor(0, 48);
                display.println("Press knob to");
                display.print("confirm offset");
                firstEntry = false;
            }

            bool timeChanged = false;
            if (encoderPos > encoderPosPrev) {
                UTC_STD_Offset++;
                if (UTC_STD_Offset > 11) UTC_STD_Offset = -12;
                timeChanged = true;
            } else if (encoderPos < encoderPosPrev) {
                UTC_STD_Offset--;
                if (UTC_STD_Offset < -12) UTC_STD_Offset = 12;
                timeChanged = true;
            }

            time_t baseTime = timeClient.getEpochTime();
            time_t localTime = baseTime + (UTC_STD_Offset * 3600);
            setTime(localTime);

            mySTD = stdRules[currentTimeZone];
            myDST = dstRules[currentTimeZone];
            mySTD.offset = UTC_STD_Offset * 60;
            myDST.offset = mySTD.offset;
            if (enableDST) myDST.offset += 60;
            myTZ = Timezone(myDST, mySTD);

            menuTimer = now();

            display.fillRect(0, 16, 128, 16, BLACK);
            display.setCursor(0, 16);
            display.setTextColor(WHITE, BLACK);
            display.print("    UTC ");
            display.print(UTC_STD_Offset >= 0 ? "+ " : "- ");
            display.print(abs(UTC_STD_Offset));
            display.print(" hours");

            static unsigned long lastDisplayUpdate = 0;
            if (timeChanged || millis() - lastDisplayUpdate >= 200) {
                displayTime();
                lastDisplayUpdate = millis();
              }
            }
            break;

        case AUTO_SHUTOFF_ENABLE:
            if (encoderPos != encoderPosPrev) enableAutoShutoff = !enableAutoShutoff;
            // Fall through to AUTO_SHUTOFF

        case AUTO_SHUTOFF_OFFTIME:
            if (menu == AUTO_SHUTOFF_OFFTIME) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = autoShutoffOfftime;
                autoShutoffOfftime = mod(encoderPos, 96);
            }
            // Fall through to AUTO_SHUTOFF

        case AUTO_SHUTOFF_ONTIME:
            if (menu == AUTO_SHUTOFF_ONTIME) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = autoShutoffOntime;
                autoShutoffOntime = mod(encoderPos, 96);
            }
            // Fall through to AUTO_SHUTOFF

        case AUTO_SHUTOFF: {
            int hr, mn;
            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.println("AUTO SHUT-OFF");
            display.setCursor(0, 16);

            if (menu == AUTO_SHUTOFF) setHighlight(0, 4);
            display.print("Enable");
            display.setTextColor(WHITE, BLACK);
            display.print("        ");
            if (menu == AUTO_SHUTOFF_ENABLE) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.println(enableAutoShutoff ? "On " : "Off");

            if (menu == AUTO_SHUTOFF) setHighlight(1, 4);
            else display.setTextColor(WHITE, BLACK);
            display.print("Turn Off Time");
            display.setTextColor(WHITE, BLACK);
            display.print(" ");
            if (menu == AUTO_SHUTOFF_OFFTIME) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            fifteenMinToHM(hr, mn, autoShutoffOfftime);
            sprintf(timestr, "%d%s%d", hr, colonDigit(mn), mn);
            display.println(timestr);

            if (menu == AUTO_SHUTOFF) setHighlight(2, 4);
            else display.setTextColor(WHITE, BLACK);
            display.print("Turn On Time");
            display.setTextColor(WHITE, BLACK);
            display.print("  ");
            if (menu == AUTO_SHUTOFF_ONTIME) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            fifteenMinToHM(hr, mn, autoShutoffOntime);
            sprintf(timestr, "%d%s%d", hr, colonDigit(mn), mn);
            display.println(timestr);

            if (menu == AUTO_SHUTOFF) setHighlight(3, 4);
            else display.setTextColor(WHITE, BLACK);
            display.println("Return");
            break;
        }

        case SHOW_DATE:
            if (encoderPos != encoderPosPrev) showDate = !showDate;
            // Fall through to SHOW_DATE_MENU

        case SHOW_DATE_INTERVAL:
            if (menu == SHOW_DATE_INTERVAL) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = date_interval_indx;
                date_interval_indx = mod(encoderPos, date_num_intervals);
                showdate_interval = date_intervals[date_interval_indx];
            }
            // Fall through to SHOW_DATE_MENU

        case SHOW_DATE_SECOND:
            if (menu == SHOW_DATE_SECOND) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = showdate_second;
                showdate_second = mod(encoderPos, 51);
            }
            // Fall through to SHOW_DATE_MENU

        case SHOW_DATE_DURATION:
            if (menu == SHOW_DATE_DURATION) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = showdate_duration;
                date_duration_indx = mod(encoderPos, date_duration_num_intervals);
                showdate_duration = date_duration_intervals[date_duration_indx];
            }
            // Fall through to SHOW_DATE_MENU

        case SHOW_DATE_FORMAT:
            if (menu == SHOW_DATE_FORMAT) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = dateFormat;
                date_format_indx = mod(encoderPos, date_format_num_intervals);
                dateFormat = date_format_intervals[date_format_indx];
            }
            // Fall through to SHOW_DATE_MENU

        case SHOW_DATE_MENU:
            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.println("DATE OPTIONS");
            display.setCursor(0, 16);

            if (menu == SHOW_DATE_MENU) setHighlight(0, 6);
            display.print("Show Date");
            display.setTextColor(WHITE, BLACK);
            display.print("       ");
            if (menu == SHOW_DATE) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.println(showDate ? "On " : "Off");

            if (menu == SHOW_DATE_MENU) setHighlight(1, 6);
            else display.setTextColor(WHITE, BLACK);
            display.print("Start at Second");
            display.setTextColor(WHITE, BLACK);
            display.print(" ");
            if (menu == SHOW_DATE_SECOND) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.println(showdate_second);

            if (menu == SHOW_DATE_MENU) setHighlight(2, 6);
            else display.setTextColor(WHITE, BLACK);
            display.print("Duration");
            display.setTextColor(WHITE, BLACK);
            display.print("        ");
            if (menu == SHOW_DATE_DURATION) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.print(showdate_duration);
            display.println("sec");

            if (menu == SHOW_DATE_MENU) setHighlight(3, 6);
            else display.setTextColor(WHITE, BLACK);
            display.print("Interval");
            display.setTextColor(WHITE, BLACK);
            display.print("        ");
            if (menu == SHOW_DATE_INTERVAL) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.print(showdate_interval);
            display.println("min");

            if (menu == SHOW_DATE_MENU) setHighlight(4, 6);
            else display.setTextColor(WHITE, BLACK);
            display.print("Format");
            display.setTextColor(WHITE, BLACK);
            display.print("       ");
            if (menu == SHOW_DATE_FORMAT) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            switch (dateFormat) {
                case 0: display.println("DD:MM:YY"); break;
                case 1: display.println("MM:DD:YY"); break;
                case 2: display.println("YY:MM:DD"); break;
            }

            if (menu == SHOW_DATE_MENU) setHighlight(5, 6);
            else display.setTextColor(WHITE, BLACK);
            display.println("Return");
            break;

        case SCREENSAVER_MENU:
            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.println("SCREENSAVER OPTIONS");
            display.setCursor(0, 16);

            setHighlight(0, 4);
            display.print("Disable");
            display.setTextColor(WHITE, BLACK);
            display.print("            ");
            if (ssOption == 0) display.println("* ");
            else display.println("  ");

            setHighlight(1, 4);
            display.print("Scrolling Image");
            display.setTextColor(WHITE, BLACK);
            display.print("    ");
            if (ssOption == 1) display.println("* ");
            else display.println("  ");

            setHighlight(2, 4);
            display.print("Turn Off OLED");
            display.setTextColor(WHITE, BLACK);
            display.print("      ");
            if (ssOption == 2) display.println("* ");
            else display.println("  ");

            setHighlight(3, 4);
            display.println("Return");
            break;

        case STATIC_COLOR:
            if (menu == STATIC_COLOR) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = static_color_indx;
                static_color_indx = mod(encoderPos, static_color_num_colors);
                LED_effect = static_color_indx + 3;
                updateLEDs();
            }
            // Fall through to LED_MENU

        case LED_BRIGHTNESS:
            if (menu == LED_BRIGHTNESS) {
                if (encoderPos == 0 && encoderPosPrev == 0) encoderPos = led_brightness_indx;
                led_brightness_indx = mod(encoderPos, led_brightness_num_intervals);
                LedBrightnessPercentage = led_brightness_intervals[led_brightness_indx];
            }
            // Fall through to LED_MENU

        case LED_MENU:
            display.setCursor(0, 0);
            display.setTextColor(WHITE, BLACK);
            display.println("LED OPTIONS");
            display.setCursor(0, 16);

            if (menu == LED_MENU) setHighlight(0, 6);
            else display.setTextColor(WHITE, BLACK);
            display.print("Disable");
            display.setTextColor(WHITE, BLACK);
            display.print("            ");
            if (LED_effect == 0) display.println(" *");
            else display.println("  ");

            if (menu == LED_MENU) setHighlight(1, 6);
            else display.setTextColor(WHITE, BLACK);
            display.print("Rainbow");
            display.setTextColor(WHITE, BLACK);
            display.print("            ");
            if (LED_effect == 1) display.println(" *");
            else display.println("  ");

            if (menu == LED_MENU) setHighlight(2, 6);
            else display.setTextColor(WHITE, BLACK);
            display.print("Color Cycle");
            display.setTextColor(WHITE, BLACK);
            display.print("        ");
            if (LED_effect == 2) display.println(" *");
            else display.println("  ");

            if (menu == LED_MENU) setHighlight(3, 6);
            else display.setTextColor(WHITE, BLACK);
            display.print("Static Color");
            display.setTextColor(WHITE, BLACK);
            display.print(" ");
            if (menu == STATIC_COLOR) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            switch (LED_effect) {
                case 3: display.println("     red"); break;
                case 4: display.println(" vermil."); break;
                case 5: display.println("  orange"); break;
                case 6: display.println("   amber"); break;
                case 7: display.println("  yellow"); break;
                case 8: display.println(" chartr."); break;
                case 9: display.println("   green"); break;
                case 10: display.println("    teal"); break;
                case 11: display.println("    blue"); break;
                case 12: display.println("  violet"); break;
                case 13: display.println("  purple"); break;
                case 14: display.println(" magenta"); break;
                default: display.println(" "); break;
            }

            if (menu == LED_MENU) setHighlight(4, 6);
            else display.setTextColor(WHITE, BLACK);
            display.print("Brightness");
            display.setTextColor(WHITE, BLACK);
            display.print("       ");
            if (menu == LED_BRIGHTNESS) display.setTextColor(BLACK, WHITE);
            else display.setTextColor(WHITE, BLACK);
            display.print(LedBrightnessPercentage);
            display.println("%");

            if (menu == LED_MENU) setHighlight(5, 6);
            else display.setTextColor(WHITE, BLACK);
            display.println("Return");
            break;
    }
    display.display();
}

/**
 * Convert 15-minute intervals to hours and minutes
 */
void fifteenMinToHM(int& hours, int& minutes, int fifteenMin) {
    hours = fifteenMin / 4;
    minutes = (fifteenMin % 4) * 15;
}

/**
 * Reset WiFi settings and restart ESP
 */
void resetWiFi() {
    WiFiManager MyWifiManager;
    MyWifiManager.resetSettings();
    delay(1000);
    ESP.restart();
}

/**
 * Highlight selected menu item
 */
void setHighlight(int menuItem, int numMenuItems) {
    if (mod(encoderPos, numMenuItems) == menuItem) {
        display.setTextColor(BLACK, WHITE);
    } else {
        display.setTextColor(WHITE, BLACK);
    }
}

/**
 * Calculate modulo with positive result
 */
inline int mod(int a, int b) {
    int r = a % b;
    return r < 0 ? r + b : r;
}

/**
 * Callback for WiFiManager configuration mode
 */
void configModeCallback(WiFiManager* myWiFiManager) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("To configure Wifi,  ");
    display.println("connect to Wifi ");
    display.print("network: ");
    display.println(AP_NAME);
    display.print("password: ");
    display.println(AP_PASSWORD);
    display.println("Open 192.168.4.1");
    display.println("in web browser");
    display.println();
    display.println("Times out in 2 min");
    display.display();
}