#pragma once
#include <Arduino.h>

void logic_init();
void logic_loop();
int getTotalCalls();
const char* getPhoneNumber(int index);