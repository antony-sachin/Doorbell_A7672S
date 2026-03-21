#pragma once
#include <Arduino.h>

// --- HARDWARE PINOUTS ---
#define MODEM_TX A3
#define MODEM_RX A2
#define SD_CS_PIN 10
#define RELAY_PIN A0

// --- TIMEOUTS ---
#define CALL_TIMEOUT      60000UL
#define CALL_DURATION_LIMIT 90000UL

// --- USER CREDENTIALS ---
extern String DTMF_PASSWORD; // Managed in logic.cpp
