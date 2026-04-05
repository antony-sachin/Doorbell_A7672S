#include "sdcard.h"
#include "config.h"
#include <SPI.h>
#include <SD.h>

char dynamicCallList[MAX_CALLERS][16];
int  dynamicCallCount = 0;
bool sdListLoaded     = false;

// ---------------------------------------------------------------------------
// sd_init — initialize SD card on given CS pin
// ---------------------------------------------------------------------------
void sd_init(uint8_t csPin) {
  Serial.print(F("[SD]: Initializing SD card..."));
  if (!SD.begin(csPin)) {
    Serial.println(F(" failed or missing!"));
    sdListLoaded = false;
    return;
  }
  Serial.println(F(" OK."));
}

// ---------------------------------------------------------------------------
// sd_loadCallList — load phone numbers for a specific floor from USERS.TXT
//
// File format example:
//   1: +919876543210, +919876543211 ; Block A residents
//   2: +919876543212                ; Block B resident
//
// Returns true if floor found and at least one number loaded, false otherwise.
// ---------------------------------------------------------------------------
bool sd_loadCallList(const char* filepath, int floorNum) {
  if (!SD.exists(filepath)) {
    Serial.print(F("[SD]: File not found: ")); Serial.println(filepath);
    return false;
  }

  File file = SD.open(filepath);
  if (!file) return false;

  dynamicCallCount = 0;
  sdListLoaded     = false;

  // Build floor prefix e.g. "3:" into fixed stack buffer — no heap
  char    targetPrefix[8];
  snprintf(targetPrefix, sizeof(targetPrefix), "%d:", floorNum);
  uint8_t prefixLen = (uint8_t)strlen(targetPrefix);

  bool foundFloor = false;
  char lineBuf[96];

  while (file.available() && !foundFloor) {

    // Read one line into fixed buffer — no String heap allocation
    uint8_t i = 0;
    while (file.available() && i < (uint8_t)(sizeof(lineBuf) - 1)) {
      char c = (char)file.read();
      if (c == '\n') break;
      if (c != '\r') lineBuf[i++] = c;
    }
    lineBuf[i] = '\0';

    // Trim trailing spaces
    while (i > 0 && lineBuf[i - 1] == ' ') lineBuf[--i] = '\0';

    // Skip if this line does not start with our floor prefix
    if (strncmp(lineBuf, targetPrefix, prefixLen) != 0) continue;

    foundFloor = true;
    Serial.print(F("[SD]: Found Floor ")); Serial.println(floorNum);

    // Find colon — numbers start after it
    char* colonPtr = strchr(lineBuf, ':');
    if (!colonPtr) break;
    char* numbersStart = colonPtr + 1;

    // Strip optional comment section after ';'
    char* semiPtr = strchr(numbersStart, ';');
    if (semiPtr) *semiPtr = '\0';

    // Trim leading spaces on numbers section
    while (*numbersStart == ' ') numbersStart++;

    // Tokenise by comma — strtok operates in-place, zero heap use
    char* token = strtok(numbersStart, ",");
    while (token != NULL && dynamicCallCount < MAX_CALLERS) {

      // Trim leading spaces on token
      while (*token == ' ') token++;

      // Trim trailing spaces on token
      char* end = token + strlen(token) - 1;
      while (end > token && *end == ' ') *end-- = '\0';

      uint8_t len = (uint8_t)strlen(token);
      if (len > 5 && len < 16) {
        strncpy(dynamicCallList[dynamicCallCount], token, 15);
        dynamicCallList[dynamicCallCount][15] = '\0';  // guarantee null-termination
        Serial.print(F(" -> Loaded Number: "));
        Serial.println(dynamicCallList[dynamicCallCount]);
        dynamicCallCount++;
      }
      token = strtok(NULL, ",");
    }
  }

  file.close();
  sdListLoaded = (dynamicCallCount > 0);
  return sdListLoaded;
}

// ---------------------------------------------------------------------------
// sd_isNumberRegistered — scan entire USERS.TXT for a phone number.
//
// Searches every floor line in the file.
// Returns true if number found, false if not found or SD error.
// Used to validate incoming calls from residents.
// ---------------------------------------------------------------------------
bool sd_isNumberRegistered(const char* filepath, const char* number) {
  if (!SD.exists(filepath)) return false;

  File file = SD.open(filepath);
  if (!file) return false;

  char lineBuf[96];

  while (file.available()) {

    // Read one line into fixed buffer
    uint8_t i = 0;
    while (file.available() && i < (uint8_t)(sizeof(lineBuf) - 1)) {
      char c = (char)file.read();
      if (c == '\n') break;
      if (c != '\r') lineBuf[i++] = c;
    }
    lineBuf[i] = '\0';

    // Trim trailing spaces
    while (i > 0 && lineBuf[i - 1] == ' ') lineBuf[--i] = '\0';

    // Skip empty lines and comment lines
    if (i == 0 || lineBuf[0] == '#') continue;

    // Find colon — everything after it is numbers
    char* colonPtr = strchr(lineBuf, ':');
    if (!colonPtr) continue;
    char* numbersStart = colonPtr + 1;

    // Strip comment section after ';'
    char* semiPtr = strchr(numbersStart, ';');
    if (semiPtr) *semiPtr = '\0';

    // Trim leading spaces
    while (*numbersStart == ' ') numbersStart++;

    // Tokenise by comma and compare each number
    char* token = strtok(numbersStart, ",");
    while (token != NULL) {

      // Trim leading spaces on token
      while (*token == ' ') token++;

      // Trim trailing spaces on token
      char* end = token + strlen(token) - 1;
      while (end > token && *end == ' ') *end-- = '\0';

      if (strcmp(token, number) == 0) {
        file.close();
        return true;   // found — registered number
      }

      token = strtok(NULL, ",");
    }
  }

  file.close();
  return false;   // not found in any floor
}

// ---------------------------------------------------------------------------
// sd_loadPassword — read DTMF unlock password from a file (e.g. KEYS.TXT)
//
// File should contain just the password digits on the first line e.g.:
//   1234
//
// Falls back to default password defined in logic.cpp if file missing.
// ---------------------------------------------------------------------------
void sd_loadPassword(const char* filepath) {
  if (!SD.exists(filepath)) {
    Serial.println(F("[SD]: keys.TXT not found. Using default code."));
    return;
  }

  File file = SD.open(filepath);
  if (file) {
    // Fixed buffer — passwords should never exceed 15 chars
    char    buf[16];
    uint8_t i = 0;
    while (file.available() && i < (uint8_t)(sizeof(buf) - 1)) {
      char c = (char)file.read();
      if (c == '\n' || c == '\r') break;
      buf[i++] = c;
    }
    buf[i] = '\0';

    // Trim trailing spaces
    while (i > 0 && buf[i - 1] == ' ') buf[--i] = '\0';

    if (i > 0) {
      DTMF_PASSWORD = buf;  // single assignment — minimal heap touch
      Serial.print(F("[SD]: Password loaded: ")); Serial.println(DTMF_PASSWORD);
    }
    file.close();
  }
}

// ---------------------------------------------------------------------------
// sd_getTotalFloors — count how many valid floor entries exist in USERS.TXT
//
// A valid floor line must:
//   - Start with a digit 1–9 (the floor number)
//   - Contain a colon separating floor number from phone numbers
//   - Not be a comment line (starting with #)
//
// Called once when visitor first presses a button so the escalation logic
// knows the ceiling — how many floors exist — without hardcoding it.
// ---------------------------------------------------------------------------
int sd_getTotalFloors(const char* filepath) {
  if (!SD.exists(filepath)) {
    Serial.println(F("[SD]: sd_getTotalFloors — file not found"));
    return 0;
  }

  File file = SD.open(filepath);
  if (!file) return 0;

  int  count = 0;
  char lineBuf[96];

  while (file.available()) {

    // Read one line into fixed buffer
    uint8_t i = 0;
    while (file.available() && i < (uint8_t)(sizeof(lineBuf) - 1)) {
      char c = (char)file.read();
      if (c == '\n') break;
      if (c != '\r') lineBuf[i++] = c;
    }
    lineBuf[i] = '\0';

    // Trim trailing spaces
    while (i > 0 && lineBuf[i - 1] == ' ') lineBuf[--i] = '\0';

    // Skip empty lines and comment lines
    if (i == 0 || lineBuf[0] == '#') continue;

    // Valid floor line: starts with digit 1–9 and contains a colon
    if (lineBuf[0] >= '1' && lineBuf[0] <= '9' &&
        strchr(lineBuf, ':') != NULL) {
      count++;
      Serial.print(F("[SD]: Floor entry found: ")); Serial.println(lineBuf);
    }
  }

  file.close();
  Serial.print(F("[SD]: Total floor entries: ")); Serial.println(count);
  return count;
}