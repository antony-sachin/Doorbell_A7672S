#pragma once
#include <Arduino.h>

// --- HARDWARE PINOUTS ---
#define MODEM_TX A2 
#define MODEM_RX A3 
#define SD_CS_PIN 10 
#define RELAY_PIN A0 

// --- TIMEOUTS ---
#define CALL_TIMEOUT 60000       
#define CALL_DURATION_LIMIT 90000 

// --- USER CREDENTIALS ---
extern String DTMF_PASSWORD; // Managed in logic.cpp