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
// Password
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
enum SystemState { ST_IDLE, ST_RINGING, ST_IN_CALL };
SystemState state = ST_IDLE;

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------
char    modemBuffer[128] = "";
uint8_t modemLen         = 0;
char    dtmfBuffer[16]   = "";

int           currentCallIndex = 0;
unsigned long stateTimer       = 0;
bool          callWasAnswered  = false;

// ---------------------------------------------------------------------------
// Chain state
//
// chainFloor    — the floor number currently locked for this chain.
//                 Set on the FIRST button press. Stays locked for the entire
//                 timeout window regardless of which button is pressed later.
//                 0 = no active chain.
//
// chainTimer    — millis() timestamp of the last call end (NO CARRIER) or
//                 the moment the chain was armed. Used to check the 2-min
//                 window.
//
// chainActive   — true while we are inside the timeout window.
//                 Any button press within this window calls the next user on
//                 chainFloor, wrapping back to user 1 after all are tried.
//
// chainCallIdx  — the NEXT user index to dial on chainFloor. Persists across
//                 button presses and wraps around when the list is exhausted.
//
// chainLoaded   — true once the call list for chainFloor has been loaded from
//                 SD into dynamicCallList[]. We only reload when the floor
//                 changes (new chain), not on every button press.
// ---------------------------------------------------------------------------
static int           chainFloor   = 0;
static unsigned long chainTimer   = 0;
static bool          chainActive  = false;
static int           chainCallIdx = 0;
static bool          chainLoaded  = false;

// ---------------------------------------------------------------------------
// logic_init
// ---------------------------------------------------------------------------
void logic_init() {
  delay(1000);
  hw_notify(F("System Ready"), F("ready.amr"));
  Serial.println(F("[SYSTEM]: IDLE - Press a button 1-8 to call a floor"));
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
// readModem
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
  while (*line == ' ') line++;
  int len = (int)strlen(line);
  while (len > 0 && line[len - 1] == ' ') line[--len] = '\0';
  if (len == 0) return;

  Serial.print(F("[MODEM]: ")); Serial.println(line);

  // -------------------------------------------------------------------------
  // +CLCC — track whether resident actually answered
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
  // RING — incoming call detected; block keypad immediately
  // -------------------------------------------------------------------------
  if (strcmp(line, "RING") == 0) {
    if (state == ST_IDLE) {
      state = ST_RINGING;   // block keypad until call is accepted or rejected
      Serial.println(F("[SYSTEM]: Incoming call — ST_RINGING, keypad blocked"));
    }
  }

  // -------------------------------------------------------------------------
  // +CLIP — caller ID, accept registered numbers, reject unknown
  // -------------------------------------------------------------------------
  if (strncmp(line, "+CLIP:", 6) == 0) {
    if (state == ST_RINGING) {
      char callerNum[16] = "";
      char* start = strchr(line, '"');
      if (start != NULL) {
        start++;
        char* end = strchr(start, '"');
        if (end != NULL) {
          uint8_t l = (uint8_t)(end - start);
          if (l > 0 && l < 16) {
            strncpy(callerNum, start, l);
            callerNum[l] = '\0';
          }
        }
      }
      Serial.print(F("[SYSTEM]: Incoming caller: ")); Serial.println(callerNum);

      if (strlen(callerNum) > 0 &&
          sd_isNumberRegistered("/CALLERS/USERS.TXT", callerNum)) {
        Serial.println(F("[SYSTEM]: Registered — answering"));
        hw_sendCmd(F("ATA"));
        hw_waitForOK(5000);
        state           = ST_IN_CALL;
        stateTimer      = millis();
        callWasAnswered = true;
        dtmfBuffer[0]   = '\0';
        Serial.println(F("[SYSTEM]: Incoming answered -> ST_IN_CALL"));
      } else {
        Serial.println(F("[SYSTEM]: Unknown — rejecting"));
        hw_sendCmd(F("ATH"));
        hw_waitForOK(3000);
        state = ST_IDLE;   // unblock keypad after rejection
        Serial.println(F("[SYSTEM]: Rejected -> ST_IDLE"));
      }
    }
  }

  // -------------------------------------------------------------------------
  // NO CARRIER — call ended
  //   callWasAnswered = false → resident rejected/no answer → nextCall()
  //   callWasAnswered = true  → resident hung up             → resetSystem()
  // -------------------------------------------------------------------------
  if (strstr(line, "NO CARRIER") != NULL) {
    if (state == ST_RINGING) {
      // Caller hung up before we could answer — just go back to idle
      state = ST_IDLE;
      Serial.println(F("[SYSTEM]: Caller hung up before answer -> ST_IDLE"));
    } else if (state == ST_IN_CALL) {
      if (!callWasAnswered) {
        // Resident rejected or did not answer — advance to next user
        hw_notify(F("No Answer. Calling Next."), F("nextuser.amr"));
        nextCall();
      } else {
        // Resident answered but hung up without giving password
        hw_notify(F("Call Ended. System Reset."), F("invalid.amr"));
        resetSystem();
      }
    }
  }

  // -------------------------------------------------------------------------
  // DTMF — accumulate digits and check for password match
  // -------------------------------------------------------------------------
  if (strstr(line, "DTMF:") != NULL && state == ST_IN_CALL) {
    char* lastColon = strrchr(line, ':');
    if (lastColon != NULL) {
      char* digitPtr = lastColon + 1;
      while (*digitPtr == ' ') digitPtr++;
      char digit = *digitPtr;

      if (digit != '\0') {
        uint8_t dLen = (uint8_t)strlen(dtmfBuffer);
        if (dLen < (uint8_t)(sizeof(dtmfBuffer) - 1)) {
          dtmfBuffer[dLen]     = digit;
          dtmfBuffer[dLen + 1] = '\0';
        }
        Serial.print(F("[DTMF]: ")); Serial.println(digit);

        uint8_t pwdLen = (uint8_t)DTMF_PASSWORD.length();
        uint8_t bufLen = (uint8_t)strlen(dtmfBuffer);
        if (bufLen >= pwdLen &&
            strcmp(dtmfBuffer + bufLen - pwdLen, DTMF_PASSWORD.c_str()) == 0) {
          hw_sendCmd(F("ATH"));
          hw_waitForOK(5000);
          hw_unlockDoor();
          resetSystem();
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// readKeypad — button press handling
//
// RULES:
//   1. Button pressed during a call (ST_IN_CALL)
//      → ignored completely.
//
//   2. Button pressed, no active chain (chainActive = false)
//      → lock chainFloor to pressed floor, load call list, start dialing
//         from user 0. Chain becomes active on the first NO CARRIER.
//
//   3. Button pressed, chain IS active (within ESCALATION_TIMEOUT_MS)
//      → IGNORE which button was pressed — always call the next user on
//         chainFloor (index already advanced by nextCall/resetSystem).
//         If list was exhausted (chainCallIdx wrapped), user 1 is called again.
//
//   4. Chain timeout expires (> ESCALATION_TIMEOUT_MS since last call end)
//      → clear chain — next press is treated as a fresh start (Rule 2).
// ---------------------------------------------------------------------------
void readKeypad() {
  // Ignore button presses during an active or incoming call
  if (state != ST_IDLE) return;

  // --- Check if chain window has expired ---
  if (chainActive &&
      (millis() - chainTimer > ESCALATION_TIMEOUT_MS)) {
    Serial.println(F("[CHAIN]: Timeout expired — chain cleared"));
    chainActive  = false;
    chainFloor   = 0;
    chainCallIdx = 0;
    chainLoaded  = false;
  }

  char key = hw_getKey();
  if (!key) return;

  // Only handle digit buttons 1–8
  if (key < '1' || key > '8') return;

  uint8_t pressedFloor = (uint8_t)(key - '0');   // 1–8

  // -------------------------------------------------------------------------
  // CASE 1: No active chain — start fresh from pressed floor
  // -------------------------------------------------------------------------
  if (!chainActive) {
    Serial.print(F("[SYSTEM]: Button ")); Serial.print(pressedFloor);
    Serial.print(F(" — new chain, calling Floor ")); Serial.println(pressedFloor);

    if (sd_loadCallList("/CALLERS/USERS.TXT", (int)pressedFloor)) {
      chainFloor   = (int)pressedFloor;
      chainCallIdx = 0;           // start from user 1
      chainLoaded  = true;
      // chainActive arms itself in nextCall() after first NO CARRIER
      startDialing();
    } else {
      hw_notify(F("Invalid Floor or No Numbers"), F("invalid.amr"));
    }
    return;
  }

  // -------------------------------------------------------------------------
  // CASE 2: Chain IS active — any button press calls next user on chainFloor
  // -------------------------------------------------------------------------
  Serial.print(F("[CHAIN]: Button pressed within window — calling next user on Floor "));
  Serial.println(chainFloor);

  // chainCallIdx was already advanced (and possibly wrapped) by nextCall().
  // Just dial whoever is up next.
  startDialing();
}

// ---------------------------------------------------------------------------
// runStateLogic — 90-second safety timeout per call
// ---------------------------------------------------------------------------
void runStateLogic() {
  if (state == ST_IN_CALL &&
      (millis() - stateTimer > CALL_DURATION_LIMIT)) {

    hw_sendCmd(F("ATH"));
    hw_waitForOK(5000);

    hw_notify(F("Timeout. Calling Next."), F("nextuser.amr"));
    nextCall();
  }
}

// ---------------------------------------------------------------------------
// startDialing — announce and dial the user at chainCallIdx on chainFloor
// ---------------------------------------------------------------------------
void startDialing() {
  ss.print(F("AT+CTTS=2,\"Calling Person "));
  ss.print(chainCallIdx + 1);
  ss.println(F("\""));
  Serial.print(F("[NOTIFY]: Calling Person ")); Serial.println(chainCallIdx + 1);
  hw_waitForTTS(10000);

  hw_sendCmd(F("ATH"));
  hw_waitForOK(3000);

  while (ss.available()) { ss.read(); }

  Serial.print(F("[CMD]: ATD"));
  Serial.print(getPhoneNumber(chainCallIdx));
  Serial.println(F(";"));
  ss.print(F("ATD"));
  ss.print(getPhoneNumber(chainCallIdx));
  ss.println(F(";"));

  state           = ST_IN_CALL;
  stateTimer      = millis();
  callWasAnswered = false;
  dtmfBuffer[0]   = '\0';
  Serial.println(F("[SYSTEM]: Dialling -> ST_IN_CALL"));
}

// ---------------------------------------------------------------------------
// nextCall — called after NO CARRIER with callWasAnswered = false,
//            and after the 90-second safety timeout.
//
// Advances chainCallIdx to the next user on chainFloor.
// Wraps back to 0 when the end of the list is reached.
// Arms chainActive so the next button press (any button) continues the chain.
// Resets call state but NOT chain state.
// ---------------------------------------------------------------------------
void nextCall() {
  delay(500);

  // Arm the chain window (or refresh it if already active)
  chainActive = true;
  chainTimer  = millis();

  // Advance index — wrap to 0 when list is exhausted
  chainCallIdx++;
  if (chainCallIdx >= getTotalCalls()) {
    Serial.println(F("[CHAIN]: All users on floor tried — wrapping back to user 1"));
    chainCallIdx = 0;
  }

  // Reset call state — chain state is preserved
  state           = ST_IDLE;
  dtmfBuffer[0]   = '\0';
  callWasAnswered = false;
  stateTimer      = 0;

  Serial.print(F("[CHAIN]: Floor ")); Serial.print(chainFloor);
  Serial.print(F(" — next user index: ")); Serial.println(chainCallIdx);
  Serial.println(F("[SYSTEM]: IDLE — press any button within timeout to call next user"));
}

// ---------------------------------------------------------------------------
// resetSystem — return to idle, clear ALL state including chain.
//               Called when: resident answered and hung up, door unlocked,
//               or incoming call ended.
// ---------------------------------------------------------------------------
void resetSystem() {
  state           = ST_IDLE;
  dtmfBuffer[0]   = '\0';
  callWasAnswered = false;
  sdListLoaded    = false;
  dynamicCallCount = 0;
  currentCallIndex = 0;
  stateTimer      = 0;

  // Clear chain state completely
  chainActive  = false;
  chainFloor   = 0;
  chainCallIdx = 0;
  chainLoaded  = false;
  chainTimer   = 0;

  Serial.println(F("[SYSTEM]: IDLE - Press a button 1-8 to call a floor"));
}