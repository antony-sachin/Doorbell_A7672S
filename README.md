# Cellular Multi-Apartment Intercom System

This repository contains professional-grade firmware for a highly scalable cellular intercom system designed for multi-tenant complexes. Powered by a dedicated embedded microcontroller (e.g., ESP32, STM32, or custom board) and the SIMCom A7672S LTE module, the system allows visitors to route calls to specific apartments via a matrix keypad. Residents can then unlock the physical door remotely using a secure DTMF passcode.

### 🚀 Core Architecture & Features

* **Infinite Scalability (On-Demand Memory):** To bypass standard MCU SRAM limitations, the system does not load static phone directories into active memory. Instead, it dynamically parses an SD card (`USERS.TXT`) at the exact moment a visitor initiates a call. This keeps memory overhead near zero, allowing the system to easily support hundreds of apartments.
* **Advanced LTE Network Handling:** Leveraging the A7672S module, the firmware actively monitors cellular release codes (`+CEER`). It intelligently differentiates between a resident actively declining a call (Codes 16/17) and a network timeout/drop (Code 31), allowing it to accurately route to the next available number or safely reset the state machine.
* **DTMF Access Control:** Residents can trigger the physical door relay by typing a secure, SD-configurable DTMF passcode on their mobile phone keypad during an active voice call.
* **Audio Feedback Engine:** Provides Text-to-Speech (TTS) or `.amr` audio feedback directly through the A7672S audio channel, indicating system status to the visitor (e.g., "Calling Person 1", "No Answer", "Door Unlocked").

### 🛠️ Hardware Stack
* **Microcontroller:** Embedded MCU (ESP32, STM32, or similar)
* **Cellular Network:** SIMCom A7672S LTE/GSM Module
* **Storage:** MicroSD Card Module (SPI interface)
* **Input:** 4x4 Matrix Keypad
* **Output:** 5V/12V Relay Module (for electronic door strike)