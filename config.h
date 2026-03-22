#pragma once
#include <Arduino.h>

// --- HARDWARE PINOUTS ---
#define MODEM_TX A3
#define MODEM_RX A2
#define SD_CS_PIN 10
#define RELAY_PIN A0

// --- TIMEOUTS ---
#define CALL_DURATION_LIMIT 90000UL

// --- CALL LIST ---
// Maximum number of phone numbers per floor in USERS.TXT
// Increase this if a floor has more residents
// Each extra slot costs 16 bytes of RAM
#define MAX_CALLERS 5

// --- PASSWORD ---
// Defined in logic.cpp — declared here so all files can access without re-declaring
extern String DTMF_PASSWORD;