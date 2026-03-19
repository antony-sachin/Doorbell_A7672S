#include "logic.h"
#include "config.h"
#include "hardware.h"
#include "sdcard.h" 

void readModem();
void processLine(String line);
void readKeypad();
void runStateLogic();
void startDialing();
void nextCall();
void resetSystem();

String DTMF_PASSWORD = "108"; 

int getTotalCalls() { 
  return sdListLoaded ? dynamicCallCount : 0; 
}

String getPhoneNumber(int index) {
  return sdListLoaded ? String(dynamicCallList[index]) : "";
}

enum SystemState { ST_IDLE, ST_DIALING, ST_ANALYZING, ST_IN_CALL };
SystemState state = ST_IDLE;

String inputString = "", modemBuffer = "", dtmfBuffer = "";
int currentCallIndex = 0;
unsigned long stateTimer = 0;

void logic_init() {
  delay(1000);
  hw_notify(F("System Ready"), F("ready.amr"));
  Serial.println(F("[SYSTEM]: IDLE - Waiting for Floor/Apt Number"));
}

void logic_loop() {
  readModem();    
  readKeypad();   
  runStateLogic(); 
}

void readModem() {
  while (ss.available()) {
    char c = ss.read();
    if (c == '\n') { processLine(modemBuffer); modemBuffer = ""; }
    else if (c != '\r') modemBuffer += c;
  }
}

void processLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  Serial.print(F("[MODEM]: ")); Serial.println(line);

  if (line.indexOf(F("NO CARRIER")) != -1) {
    if (state == ST_DIALING) {
      hw_sendCmd(F("AT+CEER")); 
      state = ST_ANALYZING;  
      stateTimer = millis(); 
    } else if (state == ST_IN_CALL) {
      hw_notify(F("Call Ended"), F("invalid.amr"));
      resetSystem();
    }
  }

  if (state == ST_ANALYZING && line.startsWith(F("+CEER:"))) {
    // ONLY 16 (Hung up) and 17 (Busy/Declined) mean the user explicitly rejected it
    if (line.indexOf(F("\"16")) != -1 || line.indexOf(F("\"17")) != -1) {
      hw_notify(F("Call Rejected. System Reset."), F("invalid.amr"));
      resetSystem(); 
    } else {
      // Code 31 (Network Drop) and everything else means the call timed out or failed to connect
      hw_notify(F("No Answer. Calling Next."), F("nextuser.amr"));
      delay(2000); 
      nextCall();
    }
  }

  if (line.indexOf(F("DTMF:")) != -1) {
    String digit = line.substring(line.lastIndexOf(':') + 1);
    digit.trim();
    dtmfBuffer += digit;
    Serial.print(F(" [DTMF]: ")); Serial.println(digit);
    
    if (dtmfBuffer.endsWith(DTMF_PASSWORD)) {
      hw_unlockDoor(); 
      hw_sendCmd(F("ATH"));
      resetSystem();
    }
  }
}

void readKeypad() {
  char key = customKeypad.getKey();
  if (!key) return;
  
  if (key >= '0' && key <= '9') { 
    inputString += key; 
    Serial.print(F("Input Floor/Apt: ")); Serial.println(inputString); 
  } 
  else if (key == 'D' && state == ST_IDLE) {
    
    // Ensure the user typed a number before pressing 'D'
    if (inputString.length() > 0) {
      int floorNum = inputString.toInt();
      
      if (floorNum > 0) {
        Serial.print(F("[SYSTEM]: Fetching numbers for Floor: ")); Serial.println(floorNum);

        // Fetch numbers from the SD card EXACTLY when requested
        if (sd_loadCallList("/CALLERS/USERS.TXT", floorNum)) {
          currentCallIndex = 0; 
          startDialing();
        } else {
          hw_notify(F("Invalid Floor or No Numbers"), F("invalid.amr"));
          resetSystem();
        }
      }
    } else {
      Serial.println(F("[SYSTEM]: 'D' pressed without number. Ignored."));
    }
    
    inputString = ""; 
  }
}

void runStateLogic() {
  if (state == ST_DIALING && (millis() - stateTimer > CALL_TIMEOUT)) {
    hw_notify(F("Timeout. Calling Next."), F("nextuser.amr"));
    nextCall();
  }
}

void startDialing() {
  hw_notify("Calling Person " + String(currentCallIndex + 1), F("calling.amr"));
  delay(2000); 
  hw_sendCmd(F("ATH")); 
  delay(1000);
  
  while(ss.available()) { ss.read(); } 

  ss.print(F("ATD")); 
  ss.print(getPhoneNumber(currentCallIndex)); 
  ss.println(F(";"));
  
  state = ST_DIALING; 
  stateTimer = millis(); 
  dtmfBuffer = ""; 
}

void nextCall() { 
  hw_sendCmd(F("ATH")); 
  delay(2000); 
  currentCallIndex++; 
  
  if (currentCallIndex < getTotalCalls()) {
    startDialing(); 
  } else {
    hw_notify(F("No one answered. System Reset."), F("invalid.amr"));
    resetSystem();
  }
}

void resetSystem() { 
  state = ST_IDLE; 
  inputString = ""; 
  dtmfBuffer = ""; 
  
  // Wipe the memory array so it is clean for the next visitor
  sdListLoaded = false;
  dynamicCallCount = 0;
  
  Serial.println(F("[SYSTEM]: IDLE - Waiting for Floor/Apt Number")); 
}