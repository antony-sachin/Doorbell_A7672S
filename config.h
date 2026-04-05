#pragma once
#include <Arduino.h>

// --- HARDWARE PINOUTS ---
#define MODEM_TX A3
#define MODEM_RX A2
#define SD_CS_PIN 10
#define RELAY_PIN A0

// --- CALL DURATION LIMIT ---
// Maximum time (ms) a single outgoing call is allowed to ring or stay active
// before the system force-hangs up and moves to the next user on the floor.
// Applies to every ATD call made by startDialing().
// 90 000 ms = 90 seconds.
#define CALL_DURATION_LIMIT 90000UL

// --- CHAIN TIMEOUT ---
// After a call ends (no answer / declined), the system holds a "chain window"
// during which ANY button press continues calling the next user on the SAME
// floor — regardless of which button was pressed.
// If the visitor does not press any button before this window expires, the
// chain is cleared and the next button press starts a completely fresh call
// to whichever floor that button maps to.
// 120 000 ms = 2 minutes.
#define ESCALATION_TIMEOUT_MS 120000UL

// --- CALL LIST ---
// Maximum number of phone numbers per floor in USERS.TXT
#define MAX_CALLERS 5

// --- PASSWORD ---
extern String DTMF_PASSWORD;