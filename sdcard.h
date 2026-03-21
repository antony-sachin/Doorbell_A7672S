#pragma once
#include <Arduino.h>

#define MAX_CALLERS 5

extern char dynamicCallList[MAX_CALLERS][16];
extern int  dynamicCallCount;
extern bool sdListLoaded;

void sd_init(uint8_t csPin);
bool sd_loadCallList(const char* filepath, int floorNum);
void sd_loadPassword(const char* filepath);
