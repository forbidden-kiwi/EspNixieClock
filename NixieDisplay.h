#ifndef NixieDisplay_h
#define NixieDisplay_h

#include "Arduino.h" // TODO Remove Arduino dependency
#include "Globals.h"

//#define In12Nixies

// Location of each segment in each tube the 64 bit shift register

#ifdef In12Nixies
// IN-12 Clock
const byte UpperLeftDot   = 31;
const byte LowerLeftDot   = 30;
const byte UpperRightDot  = 63;
const byte LowerRightDot  = 62;
const byte hourTens[]     = {6,5,4,3,2,1,0,9,8,7};
const byte hourUnits[]    = {16,15,14,13,12,11,10,19,18,17};
const byte minuteTens[]   = {26,25,24,23,22,21,20,29,28,27};
const byte minuteUnits[]  = {38,37,36,35,34,33,32,41,40,39};
const byte secondTens[]   = {48,47,46,45,44,43,42,51,50,49};
const byte secondUnits[]  = {58,57,56,55,54,53,52,61,60,59};
#else
// Z570M, IN-16, IN-17 Clocks
const byte UpperLeftDot   = 31;
const byte LowerLeftDot   = 30;
const byte UpperRightDot  = 63;
const byte LowerRightDot  = 62;
const byte hourTens[]     = {9,0,1,2,3,4,5,6,7,8};
const byte hourUnits[]    = {19,10,11,12,13,14,15,16,17,18};
const byte minuteTens[]   = {29,20,21,22,23,24,25,26,27,28};
const byte minuteUnits[]  = {41,32,33,34,35,36,37,38,39,40};
const byte secondTens[]   = {51,42,43,44,45,46,47,48,49,50};
const byte secondUnits[]  = {61,52,53,54,55,56,57,58,59,60};
#endif


class NixieDisplay {
  public:
    NixieDisplay();
    void begin();
    void enableSegment(byte segment);
    void disableSegments(const byte segments[], int count);
    void disableAllSegments();
    void disableSegment(byte segment);
    void updateDisplay();
    void runSlotMachine();
  private: 
    // Frame of data to be shifted into 64 bit HV shift register
    byte _frame[8]; // 8 bytes = 64 bits = 6 digits @ 10 bits + 4 dots @ 1 bit
};

#endif
