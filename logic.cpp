#include "logic.h"
#include "config.h"
#include "hardware.h"
#include "sdcard.h"

// --- Forward declarations ---
void readModem();
void processLine(char* line);
void readKeypad();
void runStateLogic();
void startDialing();
void nextCall();
void resetSystem();

// --- Password (kept as String for single-assignment compatibility) ---
String DTMF_PASSWORD = "108";

int getTotalCalls() {
  return sdListLoaded ? dynamicCallCount : 0;
}

const char* getPhoneNumber(int index) {
  return sdListLoaded ? dynamicCallList[index] : "";
}

// --- State machine ---
enum SystemState { ST_IDLE, ST_IN_CALL };
SystemState state = ST_IDLE;

// --- Fixed buffers — no heap String objects ---
char    inputString[8]   = "";   // Floor/apt digits from keypad
char    modemBuffer[128] = "";   // One line from modem UART
uint8_t modemLen         = 0;
char    dtmfBuffer[16]   = "";   // DTMF digits accumulated during a call

int           currentCallIndex = 0;
unsigned long stateTimer       = 0;

// --- Call answered flag ---
// Set true when +CLCC reports status 0 (call active = resident picked up).
// Cleared at the start of every new call in startDialing() and resetSystem().
//
// Decision on NO CARRIER:
//   callWasAnswered = false → not answered OR declined mid-ring → nextCall()
//   callWasAnswered = true  → answered then hung up without password → resetSystem()
//
bool callWasAnswered = false;

// ---------------------------------------------------------------------------
void logic_init() {
  delay(1000);
  hw_notify(F("System Ready"), F("ready.amr"));
  Serial.println(F("[SYSTEM]: IDLE - Waiting for Floor/Apt Number"));
}

void logic_loop() {
  readModem();
  readKeypad();
  runStateLogic();
}

// ---------------------------------------------------------------------------
// readModem — accumulates UART chars into a fixed buffer, calls processLine
//             on each newline. Overflow-safe: extra chars are dropped.
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
// processLine — all decisions made here, zero heap allocation.
// ---------------------------------------------------------------------------
void processLine(char* line) {
  // Trim leading spaces
  while (*line == ' ') line++;

  // Trim trailing spaces
  int len = (int)strlen(line);
  while (len > 0 && line[len - 1] == ' ') line[--len] = '\0';

  if (len == 0) return;

  Serial.print(F("[MODEM]: ")); Serial.println(line);

  // --- +CLCC: watch for status 0 (call active = answered) ---
  // Format: +CLCC: <idx>,<dir>,<status>,<mode>,<mpty>,...
  // status 0 = active (answered)
  // status 2 = dialing
  // status 3 = alerting/ringing on remote end
  // status 6 = disconnected
  if (strncmp(line, "+CLCC:", 6) == 0) {
    // Walk to the 3rd comma-separated field (status)
    char* p = line + 6;
    int commas = 0;
    while (*p != '\0' && commas < 2) {
      if (*p == ',') commas++;
      p++;
    }
    while (*p == ' ') p++;  // skip any spaces before the digit

    if (*p == '0') {
      callWasAnswered = true;
      Serial.println(F("[SYSTEM]: Call answered (CLCC status 0)"));
    }
  }

  // --- NO CARRIER ---
  //
  //  callWasAnswered = false:
  //    Covers BOTH "rang out with no answer" AND "declined mid-ring".
  //    In both cases the resident did not engage → call next person.
  //
  //  callWasAnswered = true:
  //    Resident picked up but hung up without entering the correct password.
  //    No point calling next person → reset and wait for a new visitor.
  //
  if (strstr(line, "NO CARRIER") != NULL) {
    if (state == ST_IN_CALL) {
      if (!callWasAnswered) {
        hw_notify(F("No Answer. Calling Next."), F("nextuser.amr"));
        delay(2000);
        nextCall();
      } else {
        hw_notify(F("Call Ended. System Reset."), F("invalid.amr"));
        resetSystem();
      }
    }
  }

  // --- DTMF handling ---
  // Modem format:  +DTMF: 1   (space between ':' and digit)
  if (strstr(line, "DTMF:") != NULL) {
    char* lastColon = strrchr(line, ':');
    if (lastColon != NULL) {
      char* digitPtr = lastColon + 1;
      while (*digitPtr == ' ') digitPtr++;  // skip space after ':'
      char digit = *digitPtr;

      if (digit != '\0') {
        uint8_t dLen = (uint8_t)strlen(dtmfBuffer);
        if (dLen < (uint8_t)(sizeof(dtmfBuffer) - 1)) {
          dtmfBuffer[dLen]     = digit;
          dtmfBuffer[dLen + 1] = '\0';
        }
        Serial.print(F(" [DTMF]: ")); Serial.println(digit);

        // Check if dtmfBuffer ends with DTMF_PASSWORD
        uint8_t pwdLen = (uint8_t)DTMF_PASSWORD.length();
        uint8_t bufLen = (uint8_t)strlen(dtmfBuffer);
        if (bufLen >= pwdLen &&
            strcmp(dtmfBuffer + bufLen - pwdLen, DTMF_PASSWORD.c_str()) == 0) {
          hw_unlockDoor();
          hw_sendCmd(F("ATH"));
          resetSystem();
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// readKeypad — builds inputString in a fixed char buffer.
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
// runStateLogic — safety timeout in case NO CARRIER never arrives.
// ---------------------------------------------------------------------------
void runStateLogic() {
  if (state == ST_IN_CALL && (millis() - stateTimer > CALL_DURATION_LIMIT)) {
    hw_notify(F("Timeout. Calling Next."), F("nextuser.amr"));
    nextCall();
  }
}

// ---------------------------------------------------------------------------
// startDialing
// ---------------------------------------------------------------------------
void startDialing() {
  // Dynamic TTS — built inline, no heap String concat
  ss.print(F("AT+CTTS=2,\"Calling Person "));
  ss.print(currentCallIndex + 1);
  ss.println(F("\""));
  Serial.print(F("[NOTIFY]: Calling Person ")); Serial.println(currentCallIndex + 1);

  delay(2000);
  hw_sendCmd(F("ATH"));
  delay(1000);

  // Flush modem receive buffer before dialling
  while (ss.available()) { ss.read(); }

  ss.print(F("ATD"));
  ss.print(getPhoneNumber(currentCallIndex));
  ss.println(F(";"));

  // ATD...;  → modem ACKs with OK (command accepted, NOT call answered).
  // No further modem event arrives when the remote picks up on voice calls.
  // We go straight to ST_IN_CALL; NO CARRIER signals the end of the call.
  state           = ST_IN_CALL;
  stateTimer      = millis();
  callWasAnswered = false;
  dtmfBuffer[0]   = '\0';
  Serial.println(F("[SYSTEM]: Dialling -> ST_IN_CALL"));
}

// ---------------------------------------------------------------------------
// nextCall
// ---------------------------------------------------------------------------
void nextCall() {
  hw_sendCmd(F("ATH"));
  delay(2000);
  currentCallIndex++;

  if (currentCallIndex < getTotalCalls()) {
    startDialing();
  } else {
    hw_notify(F("No one answered. System Reset."), F("invalid.amr"));
    resetSystem();
  }
}

// ---------------------------------------------------------------------------
// resetSystem
// ---------------------------------------------------------------------------
void resetSystem() {
  state            = ST_IDLE;
  inputString[0]   = '\0';
  dtmfBuffer[0]    = '\0';
  callWasAnswered  = false;
  sdListLoaded     = false;
  dynamicCallCount = 0;
  Serial.println(F("[SYSTEM]: IDLE - Waiting for Floor/Apt Number"));
}