#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>

extern SoftwareSerial ss;

void hw_init();
void hw_sendCmd(const __FlashStringHelper* cmd);
void hw_sendCmd(const char* cmd);
void hw_waitForOK(unsigned long timeoutMs = 3000);
void hw_waitForTTS(unsigned long timeoutMs = 15000);
void hw_notify(const __FlashStringHelper* text, const __FlashStringHelper* filename);
void hw_unlockDoor();
char hw_getKey();