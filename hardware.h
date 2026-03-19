#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Keypad.h>

extern SoftwareSerial ss;
extern Keypad customKeypad;

void hw_init();
void hw_sendCmd(String cmd);
void hw_notify(String text, String filename); // Dual-purpose function
void hw_unlockDoor();