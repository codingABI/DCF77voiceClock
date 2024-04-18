/* ----------- Stuff for EEPROM ----------
 * License: 2-Clause BSD License
 * Copyright (c) 2024 codingABI
 */

// EEPROM signature, version and start address
#define EEPROMSIGNATURE 0x16
#define EEPROMVERSION 2
#define EEPROMBASEADDRESS 0

// Check, if EEPROM is initiated
bool checkEEPROMHeader() {
  int addr = EEPROMBASEADDRESS+2;
  if (EEPROM.read(addr++) != EEPROMSIGNATURE) return false;
  if (EEPROM.read(addr++) != EEPROMVERSION) return false;
  return true;
}

// Initiate EEPROM
void writeEEPROMHeader() {
  int addr = EEPROMBASEADDRESS;
  EEPROM.update(addr++, 0);
  EEPROM.update(addr++, 0);
  EEPROM.update(addr++, EEPROMSIGNATURE);
  EEPROM.update(addr++, EEPROMVERSION);
  EEPROM.update(addr++, MP3INITVOLUME);
  EEPROM.update(addr++, LANGUAGEINITID);
}

// Get MP3 volume from EEPROM
byte getEEPROMMP3Volume() {
  word addr = EEPROMBASEADDRESS+4;
  return (EEPROM.read(addr));
}

// Set MP3 volume in EEPROM
void setEEPROMMP3Volume(byte volume) {
  word addr = EEPROMBASEADDRESS+4;
  EEPROM.update(addr++,volume & 0xff);
}

// Get language from EEPROM
byte getEEPROMLanguage() {
  word addr = EEPROMBASEADDRESS+5;
  return (EEPROM.read(addr));
}

// Set language in EEPROM
void setEEPROMLanguage(byte language) {
  word addr = EEPROMBASEADDRESS+5;
  EEPROM.update(addr++,language & 0xff);
}