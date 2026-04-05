#pragma once
#include <Arduino.h>
#include "config.h"

extern char dynamicCallList[MAX_CALLERS][16];
extern int  dynamicCallCount;
extern bool sdListLoaded;

void sd_init(uint8_t csPin);
bool sd_loadCallList(const char* filepath, int floorNum);
void sd_loadPassword(const char* filepath);
bool sd_isNumberRegistered(const char* filepath, const char* number);
int  sd_getTotalFloors(const char* filepath);   // ← NEW