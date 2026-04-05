#include "hardware.h"
#include "config.h"

SoftwareSerial ss(MODEM_RX, MODEM_TX);

// ---------------------------------------------------------------------------
// 4x2 matrix keypad
//
// Wiring:
//        D2 (Col1)   D3 (Col2)
//           |            |
// D4 (Row1)[Fl.1]      [Fl.2]
// D5 (Row2)[Fl.3]      [Fl.4]
// D6 (Row3)[Fl.5]      [Fl.6]
// D7 (Row4)[Fl.7]      [Fl.8]
//
// Scanning method:
//   - All pins start as INPUT_PULLUP (HIGH)
//   - Pull one column LOW at a time (OUTPUT LOW)
//   - Read each row pin — LOW means that button is pressed
//   - Restore column to INPUT_PULLUP after scanning
//   - Debounce: reading must be stable for DEBOUNCE_MS before registering
// ---------------------------------------------------------------------------

#define MATRIX_ROWS 4
#define MATRIX_COLS 2
#define DEBOUNCE_MS 30

// Column pins driven LOW one at a time during scan
static const uint8_t COL_PINS[MATRIX_COLS] = {2, 3};

// Row pins read during scan
static const uint8_t ROW_PINS[MATRIX_ROWS] = {4, 5, 6, 7};

// Floor number mapping [row][col]
// [0][0]=Fl1  [0][1]=Fl2
// [1][0]=Fl3  [1][1]=Fl4
// [2][0]=Fl5  [2][1]=Fl6
// [3][0]=Fl7  [3][1]=Fl8
static const char KEY_MAP[MATRIX_ROWS][MATRIX_COLS] = {
  {'2', '1'},
  {'4', '3'},
  {'6', '5'},
  {'8', '7'}
};

// Debounce state — one slot per button (4 rows x 2 cols = 8 buttons)
static bool     btnStable[MATRIX_ROWS][MATRIX_COLS]     = {};
static bool     btnWasPressed[MATRIX_ROWS][MATRIX_COLS] = {};
static uint32_t btnLastChange[MATRIX_ROWS][MATRIX_COLS] = {};

// ---------------------------------------------------------------------------
// initMatrix — set all row pins as INPUT_PULLUP, all col pins as INPUT_PULLUP
//              Columns are only driven LOW momentarily during scan
// ---------------------------------------------------------------------------
static void initMatrix() {
  for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
    pinMode(ROW_PINS[r], INPUT_PULLUP);
  }
  for (uint8_t c = 0; c < MATRIX_COLS; c++) {
    pinMode(COL_PINS[c], INPUT_PULLUP);
  }
}

// ---------------------------------------------------------------------------
// hw_getKey — scan 4x2 matrix with debounce
//
// For each column:
//   1. Drive column LOW (OUTPUT)
//   2. Read all 4 row pins
//   3. Restore column to INPUT_PULLUP
//   4. Apply debounce — only return key on stable falling edge
//
// Returns floor character '1'-'8' on confirmed press, '\0' if nothing.
// Only one key returned per call even if multiple pressed simultaneously.
// ---------------------------------------------------------------------------
char hw_getKey() {
  uint32_t now    = millis();
  char     result = '\0';

  for (uint8_t c = 0; c < MATRIX_COLS; c++) {

    // Drive this column LOW so pressed buttons pull row pins LOW
    pinMode(COL_PINS[c], OUTPUT);
    digitalWrite(COL_PINS[c], LOW);
    delayMicroseconds(10);   // brief settle time for pin to stabilise

    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {

      bool reading = (digitalRead(ROW_PINS[r]) == LOW);  // LOW = pressed

      // If raw reading changed from last stable state, restart debounce timer
      if (reading != btnStable[r][c]) {
        btnStable[r][c]     = reading;
        btnLastChange[r][c] = now;
      }

      // Only act after reading has been stable for DEBOUNCE_MS
      if ((now - btnLastChange[r][c]) >= DEBOUNCE_MS) {

        if (btnStable[r][c] && !btnWasPressed[r][c]) {
          // Confirmed press — falling edge detected
          btnWasPressed[r][c] = true;
          result = KEY_MAP[r][c];   // capture key, continue scan to update state
        }

        if (!btnStable[r][c]) {
          // Button released — reset so next press is detected
          btnWasPressed[r][c] = false;
        }
      }
    }

    // Restore column to INPUT_PULLUP — avoids columns interfering with each other
    pinMode(COL_PINS[c], INPUT_PULLUP);
    delayMicroseconds(10);   // settle before next column
  }

  return result;
}

// ---------------------------------------------------------------------------
// hw_sendCmd — send AT command to modem, echo to Serial for debugging
// ---------------------------------------------------------------------------
void hw_sendCmd(const __FlashStringHelper* cmd) {
  Serial.print(F("[CMD]: ")); Serial.println(cmd);
  ss.println(cmd);
}

void hw_sendCmd(const char* cmd) {
  Serial.print(F("[CMD]: ")); Serial.println(cmd);
  ss.println(cmd);
}

// ---------------------------------------------------------------------------
// hw_waitForOK — block until modem replies "OK" or timeout expires
// ---------------------------------------------------------------------------
void hw_waitForOK(unsigned long timeoutMs) {
  char     buf[64] = "";
  uint8_t  idx     = 0;
  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    if (ss.available()) {
      char c = (char)ss.read();
      if (c == '\n') {
        buf[idx] = '\0';
        if (idx > 0 && buf[idx - 1] == '\r') buf[--idx] = '\0';
        if (idx > 0) {
          Serial.print(F("[MODEM]: ")); Serial.println(buf);
          if (strcmp(buf, "OK") == 0) return;
        }
        idx = 0; buf[0] = '\0';
      } else if (c != '\r' && idx < 62) {
        buf[idx++] = c;
      }
    }
  }
  Serial.println(F("[WARN]: hw_waitForOK timed out"));
}

// ---------------------------------------------------------------------------
// hw_waitForTTS — block until modem replies "+CTTS: 0" (TTS finished)
// ---------------------------------------------------------------------------
void hw_waitForTTS(unsigned long timeoutMs) {
  char     buf[64] = "";
  uint8_t  idx     = 0;
  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    if (ss.available()) {
      char c = (char)ss.read();
      if (c == '\n') {
        buf[idx] = '\0';
        if (idx > 0 && buf[idx - 1] == '\r') buf[--idx] = '\0';
        if (idx > 0) {
          Serial.print(F("[MODEM]: ")); Serial.println(buf);
          if (strncmp(buf, "+CTTS: 0", 8) == 0) return;
        }
        idx = 0; buf[0] = '\0';
      } else if (c != '\r' && idx < 62) {
        buf[idx++] = c;
      }
    }
  }
  Serial.println(F("[WARN]: hw_waitForTTS timed out"));
}

// ---------------------------------------------------------------------------
// hw_notify — speak TTS and wait for playback to finish before returning
// ---------------------------------------------------------------------------
void hw_notify(const __FlashStringHelper* text, const __FlashStringHelper* filename) {
  Serial.print(F("[NOTIFY]: ")); Serial.println(text);
  ss.print(F("AT+CTTS=2,\""));
  ss.print(text);
  ss.println(F("\""));
  hw_waitForTTS(15000);
}

// ---------------------------------------------------------------------------
// hw_init — configure pins and modem at startup
// ---------------------------------------------------------------------------
void hw_init() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // relay off at startup

  initMatrix();   // set up 4x2 matrix pins

  ss.begin(9600);
  delay(1000);

  hw_sendCmd(F("AT"));
  hw_waitForOK(2000);

  hw_sendCmd(F("ATE1"));
  hw_waitForOK(2000);

  hw_sendCmd(F("AT+CVHU=0"));     // ATH properly terminates voice calls
  hw_waitForOK(2000);

  hw_sendCmd(F("AT+CLIP=1"));     // enable caller ID
  hw_waitForOK(2000);

  hw_sendCmd(F("AT+CNMI=0,0,0,0,0"));
  hw_waitForOK(2000);

  hw_sendCmd(F("AT+CSDVC=1"));
  hw_waitForOK(2000);

  hw_sendCmd(F("AT+CMGF=1"));
  hw_waitForOK(2000);

  hw_sendCmd(F("AT+CMGD=1,4"));    // 4 = delete all messages
  hw_waitForOK(5000);              // SD takes a moment if inbox is full



  /* --- NEW: mic and speaker gain --- not supported 

  hw_sendCmd(F("AT+CLVL=5"));     // maximum speaker volume
  hw_waitForOK(2000);

  hw_sendCmd(F("AT+CALM=0"));        // disable auto mute
  hw_waitForOK(2000);

  hw_sendCmd(F("AT+CMIC=0,15"));     // main mic gain max (0-15)
  hw_waitForOK(2000);

  hw_sendCmd(F("AT+CRSL=100"));      // ringer sound level max (0-100)
  hw_waitForOK(2000);*/

}

// ---------------------------------------------------------------------------
// hw_unlockDoor — announce, open relay for 3 seconds, then close
// ---------------------------------------------------------------------------
void hw_unlockDoor() {
  hw_notify(F("Door Unlocked"), F("welcome.amr"));
  digitalWrite(RELAY_PIN, LOW);   // energise relay — door opens
  delay(3000);                    // hold open 3 seconds
  digitalWrite(RELAY_PIN, HIGH);  // de-energise relay — door closes
}