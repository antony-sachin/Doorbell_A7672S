#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Keypad.h>

extern SoftwareSerial ss;
extern Keypad customKeypad;

void hw_init();
void hw_sendCmd(const __FlashStringHelper* cmd);
void hw_sendCmd(const char* cmd);
void hw_notify(const __FlashStringHelper* text, const __FlashStringHelper* filename);
void hw_unlockDoor();
