#include "sdcard.h"
#include "config.h"
#include <SPI.h>
#include <SD.h>

char dynamicCallList[MAX_CALLERS][16]; 
int dynamicCallCount = 0;
bool sdListLoaded = false;

void sd_init(uint8_t csPin) {
  Serial.print(F("[SD]: Initializing SD card..."));
  if (!SD.begin(csPin)) {
    Serial.println(F(" failed or missing!"));
    sdListLoaded = false;
    return;
  }
  Serial.println(F(" OK."));
}

bool sd_loadCallList(const char* filepath, int floorNum) {
  if (!SD.exists(filepath)) {
    Serial.print(F("[SD]: File not found: ")); Serial.println(filepath);
    return false;
  }
  
  File file = SD.open(filepath);
  if (!file) return false;

  dynamicCallCount = 0;
  sdListLoaded = false;
  
  String targetPrefix = String(floorNum) + ":"; 
  bool foundFloor = false;

  while (file.available() && !foundFloor) {
    String line = file.readStringUntil('\n'); 
    line.trim(); 
    
    if (line.startsWith(targetPrefix)) {
      foundFloor = true;
      Serial.print(F("[SD]: Found Floor ")); Serial.println(floorNum);
      
      int colonIdx = line.indexOf(':');
      int semiIdx = line.indexOf(';');
      
      String numbersPart = "";
      if (semiIdx != -1) {
        numbersPart = line.substring(colonIdx + 1, semiIdx);
      } else {
        numbersPart = line.substring(colonIdx + 1);
      }
      numbersPart.trim();

      int startIdx = 0;
      while (startIdx < numbersPart.length() && dynamicCallCount < MAX_CALLERS) {
        int commaIdx = numbersPart.indexOf(',', startIdx);
        String phoneNum;
        
        if (commaIdx == -1) {
          phoneNum = numbersPart.substring(startIdx);
          startIdx = numbersPart.length(); 
        } else {
          phoneNum = numbersPart.substring(startIdx, commaIdx); 
          startIdx = commaIdx + 1; 
        }
        
        phoneNum.trim();
        
        if (phoneNum.length() > 5 && phoneNum.length() < 16) { 
          phoneNum.toCharArray(dynamicCallList[dynamicCallCount], 16);
          Serial.print(F(" -> Loaded Number: ")); Serial.println(dynamicCallList[dynamicCallCount]);
          dynamicCallCount++;
        }
      }
    }
  }
  
  file.close();
  sdListLoaded = (dynamicCallCount > 0);
  return sdListLoaded;
}

void sd_loadPassword(const char* filepath) {
  if (!SD.exists(filepath)) {
    Serial.println(F("[SD]: keys.TXT not found. Using default code."));
    return;
  }
  File file = SD.open(filepath);
  if (file) {
    String p = file.readString();
    p.trim();
    if (p.length() > 0) {
      DTMF_PASSWORD = p; 
      Serial.print(F("[SD]: Password Updated to: ")); Serial.println(DTMF_PASSWORD);
    }
    file.close();
  }
}