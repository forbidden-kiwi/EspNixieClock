#include "HvSupply.h"

/**
 * Constructor: Initializes HvSupply object
 */
HvSupply::HvSupply() {}

/**
 * Initialize high voltage supply and enable it
 */
void HvSupply::begin() {
  pinMode(PIN_HV_EN, OUTPUT);
  digitalWrite(PIN_HV_EN, HIGH); // There is now 170V on the board!
  _hvon = true;
}

/**
 * Check if high voltage supply is on
 * @return True if on, false otherwise
 */
bool HvSupply::isOn() {
  return _hvon;
}

/**
 * Turn on the high voltage supply
 */
void HvSupply::switchOn() {
  digitalWrite(PIN_HV_EN, HIGH);
  _hvon = true;
}

/**
 * Turn off the high voltage supply
 */
void HvSupply::switchOff() {
  digitalWrite(PIN_HV_EN, LOW);
  _hvon = false;
}
