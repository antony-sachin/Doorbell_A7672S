#include "logic.h"
#include "config.h"
#include "hardware.h"
#include "sdcard.h"

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
void readModem();
void processLine(char* line);
void readKeypad();
void runStateLogic();
void startDialing();
void nextCall();
void resetSystem();

// ---------------------------------------------------------------------------
// Password — loaded from SD at startup via sd_loadPassword()
// ---------------------------------------------------------------------------
String DTMF_PASSWORD = "108";

int getTotalCalls() {
  return sdListLoaded ? dynamicCallCount : 0;
}

const char* getPhoneNumber(int index) {
  return sdListLoaded ? dynamicCallList[index] : "";
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum SystemState { ST_IDLE, ST_IN_CALL };
SystemState state = ST_IDLE;

// ---------------------------------------------------------------------------
// Buffers — no heap String objects
// ---------------------------------------------------------------------------
char    inputString[8]   = "";    // floor/apt digits typed on keypad
char    modemBuffer[128] = "";    // one line accumulated from modem UART
uint8_t modemLen         = 0;
char    dtmfBuffer[16]   = "";    // DTMF digits received during active call

int           currentCallIndex = 0;
unsigned long stateTimer       = 0;

// ---------------------------------------------------------------------------
// callWasAnswered flag
//   true  — resident picked up (+CLCC status 0 seen)
//   false — rang out or declined mid-ring
//
// On NO CARRIER:
//   false → nextCall()    (try next resident)
//   true  → resetSystem() (resident hung up without correct password)
// ---------------------------------------------------------------------------
bool callWasAnswered = false;

// ---------------------------------------------------------------------------
// logic_init
// ---------------------------------------------------------------------------
void logic_init() {
  delay(1000);
  hw_notify(F("System Ready"), F("ready.amr"));  // TTS waits to finish
  Serial.println(F("[SYSTEM]: IDLE - Waiting for Floor/Apt Number"));
}

// ---------------------------------------------------------------------------
// logic_loop
// ---------------------------------------------------------------------------
void logic_loop() {
  readModem();
  readKeypad();
  runStateLogic();
}

// ---------------------------------------------------------------------------
// readModem — accumulates modem UART into a line buffer, calls processLine
// ---------------------------------------------------------------------------
void readModem() {
  while (ss.available()) {
    char c = (char)ss.read();
    if (c == '\n') {
      modemBuffer[modemLen] = '\0';
      processLine(modemBuffer);
      modemLen       = 0;
      modemBuffer[0] = '\0';
    } else if (c != '\r') {
      if (modemLen < (uint8_t)(sizeof(modemBuffer) - 1)) {
        modemBuffer[modemLen++] = c;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// processLine — react to modem URCs
// ---------------------------------------------------------------------------
void processLine(char* line) {
  // trim leading spaces
  while (*line == ' ') line++;

  // trim trailing spaces
  int len = (int)strlen(line);
  while (len > 0 && line[len - 1] == ' ') line[--len] = '\0';

  if (len == 0) return;

  Serial.print(F("[MODEM]: ")); Serial.println(line);

  // -------------------------------------------------------------------------
  // +CLCC — track call status
  //   status field (3rd comma-field):
  //     0 = active/answered
  //     2 = MO dialing
  //     3 = remote alerting/ringing
  //     6 = disconnected
  // -------------------------------------------------------------------------
  if (strncmp(line, "+CLCC:", 6) == 0) {
    char* p = line + 6;
    int commas = 0;
    while (*p != '\0' && commas < 2) {
      if (*p == ',') commas++;
      p++;
    }
    while (*p == ' ') p++;

    if (*p == '0') {
      callWasAnswered = true;
      Serial.println(F("[SYSTEM]: Call answered (CLCC status 0)"));
    }
  }

  // -------------------------------------------------------------------------
  // RING — incoming call
  // Wait for +CLIP to get caller number then check SD card.
  // Registered number → accept (do nothing, let it ring through)
  // Unknown number    → reject with ATH immediately
  //
  // Note: RING handling only when ST_IDLE.
  // ST_IN_CALL means we have an active outgoing call — RING impossible.
  // -------------------------------------------------------------------------
  if (strcmp(line, "RING") == 0) {
    if (state == ST_IDLE) {
      Serial.println(F("[SYSTEM]: Incoming call detected — waiting for caller ID"));
      // ATH will be sent from +CLIP handler if number not registered
      // If +CLIP never arrives — RING repeats and we check again next time
    }
  }

  // +CLIP — caller ID arrives after RING
  // Format: +CLIP: "+919876543210",145,"",,"",0
  if (strncmp(line, "+CLIP:", 6) == 0) {
    if (state == ST_IDLE) {

      // Extract number between first pair of quotes
      char callerNum[16] = "";
      char* start = strchr(line, '"');
      if (start != NULL) {
        start++;                           // skip opening quote
        char* end = strchr(start, '"');    // find closing quote
        if (end != NULL) {
          uint8_t len = (uint8_t)(end - start);
          if (len > 0 && len < 16) {
            strncpy(callerNum, start, len);
            callerNum[len] = '\0';
          }
        }
      }

      Serial.print(F("[SYSTEM]: Incoming caller: ")); Serial.println(callerNum);

      // Search entire USERS.TXT for this number
      if (strlen(callerNum) > 0 &&
          sd_isNumberRegistered("/CALLERS/USERS.TXT", callerNum)) {
        // Registered resident — answer the call so they can enter DTMF password
        Serial.println(F("[SYSTEM]: Registered number — answering"));
        hw_sendCmd(F("ATA"));
        hw_waitForOK(5000);
        state           = ST_IN_CALL;
        stateTimer      = millis();   // start 90s safety timeout
        callWasAnswered = true;       // resident answered — hang up → resetSystem()
        dtmfBuffer[0]   = '\0';
        Serial.println(F("[SYSTEM]: Incoming call answered -> ST_IN_CALL"));
      } else {
        // Unknown number — reject immediately
        Serial.println(F("[SYSTEM]: Unknown number — rejecting call"));
        hw_sendCmd(F("ATH"));
        hw_waitForOK(3000);
        Serial.println(F("[SYSTEM]: Incoming call rejected"));
      }
    }
  }


  if (strstr(line, "NO CARRIER") != NULL) {
    if (state == ST_IN_CALL) {
      if (!callWasAnswered) {
        // TTS finishes before nextCall because hw_notify blocks on +CTTS: 0
        hw_notify(F("No Answer. Calling Next."), F("nextuser.amr"));
        nextCall();
      } else {
        hw_notify(F("Call Ended. System Reset."), F("invalid.amr"));
        resetSystem();
      }
    }
  }

  // -------------------------------------------------------------------------
  // DTMF — accumulate digits, check for password match
  // Modem sends:  +RXDTMF: 1   (space between ':' and digit)
  // Only process DTMF when a call is active — ignore if idle (modem noise)
  // -------------------------------------------------------------------------
  if (strstr(line, "DTMF:") != NULL && state == ST_IN_CALL) {
    char* lastColon = strrchr(line, ':');
    if (lastColon != NULL) {
      char* digitPtr = lastColon + 1;
      while (*digitPtr == ' ') digitPtr++;   // skip space after ':'
      char digit = *digitPtr;

      if (digit != '\0') {
        uint8_t dLen = (uint8_t)strlen(dtmfBuffer);
        if (dLen < (uint8_t)(sizeof(dtmfBuffer) - 1)) {
          dtmfBuffer[dLen]     = digit;
          dtmfBuffer[dLen + 1] = '\0';
        }
        Serial.print(F(" [DTMF]: ")); Serial.println(digit);

        // check if dtmfBuffer ends with DTMF_PASSWORD
        uint8_t pwdLen = (uint8_t)DTMF_PASSWORD.length();
        uint8_t bufLen = (uint8_t)strlen(dtmfBuffer);
        if (bufLen >= pwdLen &&
            strcmp(dtmfBuffer + bufLen - pwdLen, DTMF_PASSWORD.c_str()) == 0) {

          // 1. Cut call FIRST — modem must be idle before any other command
          hw_sendCmd(F("ATH"));
          hw_waitForOK(5000);        // confirmed call cut

          // 2. Open door — TTS plays then relay fires (hw_notify blocks)
          hw_unlockDoor();

          // 3. Back to idle
          resetSystem();
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// readKeypad — collect floor/apt digits, trigger dialing on 'D'
// ---------------------------------------------------------------------------
void readKeypad() {
  char key = customKeypad.getKey();
  if (!key) return;

  if (key >= '0' && key <= '9') {
    uint8_t len = (uint8_t)strlen(inputString);
    if (len < (uint8_t)(sizeof(inputString) - 1)) {
      inputString[len]     = key;
      inputString[len + 1] = '\0';
    }
    Serial.print(F("Input Floor/Apt: ")); Serial.println(inputString);
  }
  else if (key == 'D' && state == ST_IDLE) {
    if (strlen(inputString) > 0) {
      int floorNum = atoi(inputString);
      if (floorNum > 0) {
        Serial.print(F("[SYSTEM]: Fetching numbers for Floor: ")); Serial.println(floorNum);
        if (sd_loadCallList("/CALLERS/USERS.TXT", floorNum)) {
          currentCallIndex = 0;
          startDialing();
        } else {
          hw_notify(F("Invalid Floor or No Numbers"), F("invalid.amr"));
          resetSystem();
        }
      }
    } else {
      Serial.println(F("[SYSTEM]: 'D' pressed without number. Ignored."));
    }
    inputString[0] = '\0';
  }
}

// ---------------------------------------------------------------------------
// runStateLogic — 90-second safety timeout
//   If NO CARRIER never arrives (modem stuck), force-hang and try next person.
// ---------------------------------------------------------------------------
void runStateLogic() {
  if (state == ST_IN_CALL && (millis() - stateTimer > CALL_DURATION_LIMIT)) {

    // 1. Cut call — wait for modem confirmation
    hw_sendCmd(F("ATH"));
    hw_waitForOK(5000);

    // 2. Advance to next person or reset
    currentCallIndex++;
    if (currentCallIndex < getTotalCalls()) {
      hw_notify(F("Timeout. Calling Next."), F("nextuser.amr"));
      startDialing();
    } else {
      hw_notify(F("No one answered. System Reset."), F("invalid.amr"));
      resetSystem();
    }
  }
}

// ---------------------------------------------------------------------------
// startDialing — announce, safety-ATH, then dial next number
// ---------------------------------------------------------------------------
void startDialing() {
  // Announce which person we are calling (dynamic TTS, no String concat)
  ss.print(F("AT+CTTS=2,\"Calling Person "));
  ss.print(currentCallIndex + 1);
  ss.println(F("\""));
  Serial.print(F("[NOTIFY]: Calling Person ")); Serial.println(currentCallIndex + 1);
  hw_waitForTTS(10000);   // wait for TTS to finish before sending next command

  // Safety ATH — clears any ghost call state before dialing
  hw_sendCmd(F("ATH"));
  hw_waitForOK(3000);

  // Flush residual modem bytes
  while (ss.available()) { ss.read(); }

  // Dial
  Serial.print(F("[CMD]: ATD")); Serial.print(getPhoneNumber(currentCallIndex)); Serial.println(F(";"));
  ss.print(F("ATD"));
  ss.print(getPhoneNumber(currentCallIndex));
  ss.println(F(";"));

  // Enter call state
  state           = ST_IN_CALL;
  stateTimer      = millis();
  callWasAnswered = false;
  dtmfBuffer[0]   = '\0';
  Serial.println(F("[SYSTEM]: Dialling -> ST_IN_CALL"));
}

// ---------------------------------------------------------------------------
// nextCall — called only after NO CARRIER (call already ended by network)
//            No ATH needed — the line is already dead.
// ---------------------------------------------------------------------------
void nextCall() {
  delay(500);
  currentCallIndex++;
  if (currentCallIndex < getTotalCalls()) {
    startDialing();
  } else {
    hw_notify(F("No one answered. System Reset."), F("invalid.amr"));
    resetSystem();
  }
}

// ---------------------------------------------------------------------------
// resetSystem — return to idle, clear all state
// ---------------------------------------------------------------------------
void resetSystem() {
  state            = ST_IDLE;
  inputString[0]   = '\0';
  dtmfBuffer[0]    = '\0';
  callWasAnswered  = false;
  sdListLoaded     = false;
  dynamicCallCount = 0;
  currentCallIndex = 0;
  stateTimer       = 0;       // prevent stale timeout firing on next call
  Serial.println(F("[SYSTEM]: IDLE - Waiting for Floor/Apt Number"));
}