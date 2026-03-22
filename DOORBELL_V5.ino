#include "hardware.h"
#include "logic.h"
#include "sdcard.h"
#include "config.h"

void setup() {
  Serial.begin(9600);
  delay(3000);
  
  hw_init();
  sd_init(SD_CS_PIN);
  
  // Only load the password on startup. No default call lists.
  sd_loadPassword("/CALLERS/keys.TXT");
  
  logic_init();
}

void loop() {
  logic_loop();
}