#include "NixieDisplay.h"

/**
 * Constructor: Initializes the frame buffer to zero
 */
NixieDisplay::NixieDisplay() {
    for (int i = 0; i < 8; i++) {
        _frame[i] = 0;
    }
}

/**
 * Initialize pins and set all segments to off
 */
void NixieDisplay::begin() {
    pinMode(PIN_HV_LE, OUTPUT);
    pinMode(PIN_HV_BL, OUTPUT);
    pinMode(PIN_HV_DATA, OUTPUT);
    pinMode(PIN_HV_CLK, OUTPUT);
    digitalWrite(PIN_HV_BL, HIGH);
    disableAllSegments();
    updateDisplay(); // Write 64 zeros into shift registers before enabling HV
}

/**
 * Enable a specific segment by setting its bit in the frame buffer
 * @param segment The segment index to enable
 */
void NixieDisplay::enableSegment(byte segment) {
    byte f = 7 - (segment / 8); // Calculate byte index
    byte b = segment % 8;       // Calculate bit index
    _frame[f] |= 1 << b;        // Set the bit
}

/**
 * Disable multiple segments by clearing their bits in the frame buffer
 * @param segments Array of segment indices to disable
 * @param count Number of segments in the array
 */
void NixieDisplay::disableSegments(const byte segments[], int count) {
    for (int i = 0; i < count; ++i) {
        byte segment = segments[i];
        disableSegment(segment);
    }
}

/**
 * Disable all segments by clearing the frame buffer
 */
void NixieDisplay::disableAllSegments() {
    for (int i = 0; i < 8; ++i) {
        _frame[i] = 0b00000000;
    }
}

/**
 * Disable a specific segment by clearing its bit in the frame buffer
 * @param segment The segment index to disable
 */
void NixieDisplay::disableSegment(byte segment) {
    byte f = 7 - (segment / 8); // Calculate byte index
    byte b = segment % 8;       // Calculate bit index
    _frame[f] &= ~(1 << b);     // Clear the bit
}

/**
 * Update the display by shifting out the frame buffer to the shift registers
 */
void NixieDisplay::updateDisplay() {
    digitalWrite(PIN_HV_LE, LOW);
    for (int i = 0; i < 8; ++i) {
        shiftOut(PIN_HV_DATA, PIN_HV_CLK, MSBFIRST, _frame[i]);
    }
    digitalWrite(PIN_HV_LE, HIGH);
}

/**
 * Start the slot machine effect with initial and target time values
 * @param startHour Starting hour value
 * @param startMinute Starting minute value
 * @param startSecond Starting second value
 * @param targetHour Target hour value
 * @param targetMinute Target minute value
 * @param targetSecond Target second value
 */
void NixieDisplay::startSlotMachine(int startHour, int startMinute, int startSecond, 
                                    int targetHour, int targetMinute, int targetSecond) {
    if (_slotMachineActive) {
        return; // Skip if slot machine is already active
    }

    _slotMachineActive = true;
    _slotMachineStartTime = millis();

    // Prepare initial digits
    int tempDigits[6] = {startHour / 10, startHour % 10, startMinute / 10, startMinute % 10, startSecond / 10, startSecond % 10};
    bool used[10] = {false};

    // Assign starting digits, avoiding duplicates
    for (int i = 0; i < 6; i++) {
        int digit = tempDigits[i];
        if (used[digit]) {
            for (int offset = 1; offset <= 9; offset++) {
                int newDigit = (digit + offset) % 10;
                if (!used[newDigit]) {
                    _startDigits[i] = newDigit;
                    used[newDigit] = true;
                    break;
                }
            }
        } else {
            _startDigits[i] = digit;
            used[digit] = true;
        }
        _digitActive[i] = true; // All tubes start active
    }

    // Set target digits
    _targetDigits[0] = targetHour / 10;
    _targetDigits[1] = targetHour % 10;
    _targetDigits[2] = targetMinute / 10;
    _targetDigits[3] = targetMinute % 10;
    _targetDigits[4] = targetSecond / 10;
    _targetDigits[5] = targetSecond % 10;
}

/**
 * Update the slot machine effect, animating digits to target values
 */
void NixieDisplay::updateSlotMachine() {
    if (!_slotMachineActive) {
        return; // Skip if slot machine is not active
    }

    unsigned long currentMillis = millis();
    unsigned long elapsedTime = currentMillis - _slotMachineStartTime;
    int steps = elapsedTime / 50;       // 50ms per step
    const int minCycleSteps = 80;       // Minimum 8 cycles

    disableAllSegments();

    const byte* digitSegments[6] = {hourTens, hourUnits, minuteTens, minuteUnits, secondTens, secondUnits};
    bool allInactive = true;

    for (int i = 0; i < 6; i++) {
        if (!_digitActive[i]) {
            enableSegment(digitSegments[i][_targetDigits[i]]); // Set to target if already inactive
            continue;
        }

        int startDigit = _startDigits[i];
        int targetDigit = _targetDigits[i];
        int currentDigit = (startDigit + steps) % 10;

        // Check if target digit is reached and minimum cycles are complete
        if (currentDigit == targetDigit && steps >= minCycleSteps) {
            _digitActive[i] = false; // Stop this tube
        }

        disableSegments(digitSegments[i], 10);
        enableSegment(digitSegments[i][currentDigit]);

        if (_digitActive[i]) allInactive = false;
    }

    // Disable colon dots
    disableSegment(UpperLeftDot);
    disableSegment(LowerLeftDot);
    disableSegment(UpperRightDot);
    disableSegment(LowerRightDot);

    updateDisplay();

    // End effect if all tubes are inactive
    if (allInactive) {
        _slotMachineActive = false;
    }
}

/**
 * Check if the slot machine effect is currently active
 * @return True if active, false otherwise
 */
bool NixieDisplay::isSlotMachineActive() {
    return _slotMachineActive;
}