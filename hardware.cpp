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

void hw_init() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  ss.begin(9600);

  hw_sendCmd(F("AT"));
  hw_sendCmd(F("ATE1"));
  hw_sendCmd(F("AT+CLIP=1"));
  hw_sendCmd(F("AT+DDET=1"));
  hw_sendCmd(F("AT+CNMI=0,0,0,0,0"));
  hw_sendCmd(F("AT+CSDVC=1"));
}

// Overload 1: Flash string (F() macro) — no heap copy
void hw_sendCmd(const __FlashStringHelper* cmd) {
  ss.println(cmd);
}

// Overload 2: char* — used when runtime string is needed (e.g. ATD number)
void hw_sendCmd(const char* cmd) {
  ss.println(cmd);
}

// Both text and filename come from flash (F() macro) — zero heap allocation
void hw_notify(const __FlashStringHelper* text, const __FlashStringHelper* filename) {
  // LOGGING: Keep serial logs exactly as before
  Serial.print(F("[NOTIFY]: ")); Serial.println(text);

  // --- TTS IS ACTIVE BY DEFAULT ---
  ss.print(F("AT+CTTS=2,\""));
  ss.print(text);
  ss.println(F("\""));

  // --- AUDIO IS COMMENTED OUT (Uncomment this and comment TTS above to switch) ---
  /*
  ss.print(F("AT+CPAMR=\"c:/"));
  ss.print(filename);
  ss.println(F("\",1"));
  */
}

void hw_unlockDoor() {
  hw_notify(F("Door Unlocked"), F("welcome.amr"));
  digitalWrite(RELAY_PIN, LOW);
  delay(3000);
  digitalWrite(RELAY_PIN, HIGH);
}
