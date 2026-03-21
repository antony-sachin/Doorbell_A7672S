#include "hardware.h"
#include "config.h"

SoftwareSerial ss(MODEM_RX, MODEM_TX);

const byte ROWS = 4;
const byte COLS = 4;
char hexaKeys[ROWS][COLS] = {
  {'1', '2', '3', 'A'}, {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'}, {'*', '0', '#', 'D'}
};
byte rowPins[ROWS] = {9, 8, 7, 6};
byte colPins[COLS] = {5, 4, 3, 2};
Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

// ---------------------------------------------------------------------------
// hw_sendCmd — send AT command to modem, log to Serial
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
// hw_waitForOK — block until modem replies "OK" or timeout expires.
//                Logs every modem line received while waiting.
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
          if (strcmp(buf, "OK") == 0) return;   // confirmed — done
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
//                 so the next command doesn't collide with ongoing speech.
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
          // "+CTTS: 0" means TTS playback finished
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
// hw_notify — send TTS and WAIT for playback to finish before returning.
//             This prevents the next AT command colliding with ongoing speech.
// ---------------------------------------------------------------------------
void hw_notify(const __FlashStringHelper* text, const __FlashStringHelper* filename) {
  Serial.print(F("[NOTIFY]: ")); Serial.println(text);

  // --- TTS ---
  ss.print(F("AT+CTTS=2,\""));
  ss.print(text);
  ss.println(F("\""));

  // Wait for TTS to complete (+CTTS: 0) before returning
  // Timeout 15s covers even long phrases
  hw_waitForTTS(15000);

  // --- AUDIO (uncomment and comment TTS block above to switch) ---
  /*
  ss.print(F("AT+CPAMR=\"c:/"));
  ss.print(filename);
  ss.println(F("\",1"));
  hw_waitForTTS(15000);
  */
}

// ---------------------------------------------------------------------------
// hw_init — configure modem at startup
// ---------------------------------------------------------------------------
void hw_init() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  ss.begin(9600);
  delay(1000);

  hw_sendCmd(F("AT"));
  hw_waitForOK(2000);

  hw_sendCmd(F("ATE1"));
  hw_waitForOK(2000);

  hw_sendCmd(F("AT+CVHU=0"));      // ATH properly terminates voice calls
  hw_waitForOK(2000);

  hw_sendCmd(F("AT+CLIP=1"));
  hw_waitForOK(2000);

  //hw_sendCmd(F("AT+DDET=1"));
  //hw_waitForOK(2000);

  hw_sendCmd(F("AT+CNMI=0,0,0,0,0"));
  hw_waitForOK(2000);

  hw_sendCmd(F("AT+CSDVC=1"));
  hw_waitForOK(2000);
}

// ---------------------------------------------------------------------------
// hw_unlockDoor — open relay AFTER call is already cut by caller
// ---------------------------------------------------------------------------
void hw_unlockDoor() {
  hw_notify(F("Door Unlocked"), F("welcome.amr"));  // TTS waits to finish
  digitalWrite(RELAY_PIN, LOW);
  delay(3000);                                       // door open 3 seconds
  digitalWrite(RELAY_PIN, HIGH);
}