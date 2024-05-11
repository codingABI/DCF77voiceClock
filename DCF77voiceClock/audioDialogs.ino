/* ----------- Stuff for the audio dialogs ----------
 * License: 2-Clause BSD License
 * Copyright (c) 2024 codingABI
 */

// Enables MP3 module
void MP3On() {
  if (g_MP3enabled || g_MP3enablePending) return;
  g_MP3Serial = new SoftwareSerial(MP3RX_PIN, MP3TX_PIN);
  g_MP3Serial->begin(9600);
  g_audio = new DFR0534(*g_MP3Serial);
  // Disconnect speaker to prevent power on noise
  pinMode(MP3MUTE_PIN,OUTPUT);
  digitalWrite(MP3MUTE_PIN,LOW);
  // Enable Vcc
  pinMode(MP3VCC_PIN,OUTPUT);
  digitalWrite(MP3VCC_PIN,HIGH);
  // Set pending mode => checkPendingAudio will do the rest after a nonblocking delay
  g_MP3powerOnMS = millis();
  g_MP3enablePending = true;
}

// Disables MP3 module
void MP3Off() {
  if (!g_MP3enabled) return;
  g_audio->setVolume(0);
  // Disconnect speaker
  pinMode(MP3MUTE_PIN,OUTPUT);
  digitalWrite(MP3MUTE_PIN,LOW);

  g_MP3Serial->end();
  delete g_audio;
  // Disable Vcc
  digitalWrite(MP3VCC_PIN,LOW);
  pinMode(MP3VCC_PIN,INPUT);

  g_MP3enabled = false;
  g_MP3enablePending = false;
  clearPendingAudio();

  delete g_MP3Serial;

  // Set pins to input
  pinMode(MP3RX_PIN,INPUT);
  pinMode(MP3TX_PIN,INPUT);
  pinMode(MP3MUTE_PIN,INPUT);
}

// Clear pening audio sequence
void clearPendingAudio() {
  g_pendingAudio.sequence[0] = '\0';
}
// Set combined audio file as pending audio sequence
void setPendingAudio(int audioID) {
  clearPendingAudio();
  addPendingAudio(audioID);
}

// Add combined audio file to pending audio sequence
void addPendingAudio(int audioID) {
  if (strlen(g_pendingAudio.sequence) >= MAXPENDINGAUDIOLENGTH-2) return; // String too long
  byte charRange = 'Z'-'A'+1;
  byte nextPosition = strlen(g_pendingAudio.sequence);

  audioID = audioID + g_languageID * AUDIO_MAXFILES; // Select language

  // Build filenames from AA to ZZ
  g_pendingAudio.sequence[nextPosition] = 'A' + (audioID/charRange);
  g_pendingAudio.sequence[nextPosition+1] = 'A' + (audioID%charRange);
  g_pendingAudio.sequence[nextPosition+2] = '\0';
}

// Check for a pending audio sequence and play it, when audio module is ready
void checkPendingAudio() {
  // Without 500ms delay after power up first commands are sometimes skipped
  // by the audio module (1000ms gives enough time for power-on noise to be finish)
  if (g_MP3enablePending && (millis()-g_MP3powerOnMS > 1000)) {
    // Start with minimum audio volume
    g_audio->setVolume(0);
    // Connect speaker
    pinMode(MP3MUTE_PIN,INPUT);
    // Set equalizer
    g_audio->setEqualizer(DFR0534::JAZZ );
    // Mark audio to be ready for playing files
    g_MP3enabled = true;
    g_MP3enablePending = false;
  }

  // When audio module is ready and an audio sequence is pending => play it
  if (g_MP3enabled && (strlen(g_pendingAudio.sequence) > 0)) {
    // Set volume
    g_audio->setVolume(g_pendingAudio.volume);

    if (g_pendingAudio.sequence[0] == '/') { // Single file (not combined)
      g_audio->playFileByName(g_pendingAudio.sequence);
    } else { // Combined
      g_audio->playCombined(g_pendingAudio.sequence);
    }
    // Clear pending sequence
    clearPendingAudio();
  }
}

/* Check for a pending audio sequence and play it, when audio module is ready,
 * prevents too much load for audio module (too much load causes extra delays on my module)
 * millis() is needed for this function => sleep mode SLEEP_MODE_PWR_SAVE is not possible
 */
void relaxedCheckPendingAudio() {
  #define MINDELAYBETWEENAUDIOMS 250
  static unsigned long lastAudioChangeMS = 0;
  if (millis()-lastAudioChangeMS > MINDELAYBETWEENAUDIOMS) {
    checkPendingAudio();
    lastAudioChangeMS=millis();
  }
}

// Say complete time (clock, date, sunrise, sunset...)
void sayCompleteTime() {
  unsigned long currentUTCTime =  getCurrentUTC();
  unsigned long currentLocalTime = UTCtoLocalTime(currentUTCTime);

  clearPendingAudio();
  sayTime(currentLocalTime);
  addPendingAudio(AUDIO_SUNDAY+weekday(currentLocalTime)-1);
  addPendingAudio(AUDIO_1st+day(currentLocalTime)-1);
  addPendingAudio(AUDIO_JANUARY+month(currentLocalTime)-1);
  addPendingAudio(AUDIO_1970+year(currentLocalTime)-1970);

  // Last time sync failed?
  if (g_weakTime) addPendingAudio(AUDIO_LASTSYNCFAILED);

  // Sunrise/sunset
  Dusk2Dawn sunRiseSet(MYLAT,MYLON,0);
  int sunriseMinutes = sunRiseSet.sunrise(year(currentUTCTime), month(currentUTCTime), day(currentUTCTime), false);
  addPendingAudio(AUDIO_SUNRISE);
  sayTime(UTCtoLocalTime(tmConvert_t(year(currentUTCTime), month(currentUTCTime), day(currentUTCTime), (byte)(sunriseMinutes/60), (byte) (sunriseMinutes%60),0)));

  int sunsetMinutes = sunRiseSet.sunset(year(currentUTCTime), month(currentUTCTime), day(currentUTCTime), false);
  addPendingAudio(AUDIO_SUNSET);
  sayTime(UTCtoLocalTime(tmConvert_t(year(currentUTCTime), month(currentUTCTime), day(currentUTCTime), (byte)(sunsetMinutes/60), (byte) (sunsetMinutes%60),0)));

  addPendingAudio(AUDIO_MAINMENU);
  addPendingAudio(AUDIO_CURRENTITEM);
  addPendingAudio(AUDIO_COMPLETETIME);
}

// Get time from DCF77 module
void setTimeFromDCF77() {
  #define DCF77TIMEOUT 900
  bool exitLoop = false;
  byte lastOutput = HIGH;
  unsigned long lastAudioCheckUTC = 0;
  unsigned long syncStartTimeUTC;
  unsigned long currentUTCTime;
  byte buzzerCounter = 0;
  byte idleCounter = 0;

  if (g_dcfAudioMode == MP3MODE) {
    setPendingAudio(AUDIO_SYNCDESC1);
    addPendingAudio(DCF77TIMEOUT/60);
    addPendingAudio(AUDIO_SYNCDESC2);
  }
  // DCF77 needs millis() and millis() works only in IDLE sleep
  // (better than noting: The ATmega328 seems to consume 50% power in IDLE mode
  // in comparison with normal operational mode)
  set_sleep_mode(SLEEP_MODE_IDLE);

  // Save ADC status
  int oldADCSRA = ADCSRA; // Backup current ADC
  // Disable ADC, because we don't need analog ports during the DCF77 sync
  ADCSRA = 0;

  // Enable Vcc for the DCF77 module
  pinMode(DCF77_VCC_PIN,OUTPUT);
  digitalWrite(DCF77_VCC_PIN,HIGH);
  // Enable interrupt for DCF77 out pin
  DCF.Start();
  syncStartTimeUTC = getCurrentUTC();
  g_weakTime = true;


  do {
    wdt_reset();
    relaxedCheckPendingAudio();

    // Power off audio module, when not needed anymore
    currentUTCTime = getCurrentUTC();
    if ((g_MP3enabled) && (currentUTCTime != lastAudioCheckUTC)) {
      if (g_audio->getStatus() == DFR0534::STOPPED){
        idleCounter++;
        if (idleCounter >= MAXIDLECHECKS) {
          MP3Off();
          idleCounter = 0;
        }
      } else idleCounter = 0;
      lastAudioCheckUTC = currentUTCTime;
    }

    // Use DCF77 signal as acoustic signal
    byte output = digitalRead(DCF77_OUT_PIN);
    if (!g_MP3enabled && (output != lastOutput)
      && ((g_dcfAudioMode == MP3MODE) || (g_dcfAudioMode == INITMODE))) {
      if ((buzzerCounter & 1) == 1) beep(MICROBEEP); // Micro beep every second signal change
      buzzerCounter++;
      lastOutput = output;
    }

    time_t DCFtime = DCF.getTime(); // Check if new DCF77 time is available
    if (DCFtime!=0) {
      setCurrentUTC(localTimeToUTC(DCFtime));
      g_initTimeSyncPending = false;
      g_weakTime = false;
      exitLoop = true;
    }

    // User input timeout expired?
    if (currentUTCTime - syncStartTimeUTC >= DCF77TIMEOUT) exitLoop = true;

    switch (g_switchButton.getButton()) {
      case SWITCHBUTTON::SHORTPRESSED:
      case SWITCHBUTTON::LONGPRESSED:
        exitLoop = true;
        break;
    }
    if (!exitLoop) {
      power_spi_disable(); // Disable SPI
      power_usart0_disable(); // Disable the USART 0 module (disables Serial....)
      power_twi_disable(); // Disable I2C, because we do not use I2C devices in this project
      sleep_mode();
    }
  }
  while (!exitLoop);

  DCF.Stop();
  // Power off Vcc for DCF77 module
  digitalWrite(DCF77_VCC_PIN,LOW);
  pinMode(DCF77_VCC_PIN,INPUT);

  // Restore ADC
  ADCSRA = oldADCSRA;

  // Reenable audio module
  if ((g_dcfAudioMode != SILENTMODE) && !g_MP3enabled) MP3On();
}

// Main menu
void menu() {
  int Vcc;
  // Menu items
  enum menuItems { MENUCOMPLETETIME, MENUTIMESET, MENUDCF77, MENUVOLUME, MENULANGUAGE, MENUVCC, MENUEXIT, MAXMENUITEMS };
  int currentMenuItem;
  int lastMenuItem = MAXMENUITEMS;
  unsigned long lastUserInputUTC = 0;
  bool exitLoop = false;
  unsigned long currentUTCTime;

  currentMenuItem = MENUCOMPLETETIME;
  lastMenuItem = currentMenuItem;

  beep(SHORTBEEP);
  // Enable audio module
  MP3On();
  // Intro audio
  setPendingAudio(AUDIO_MAINMENU);
  addPendingAudio(AUDIO_MAINMENUDESC);
  addPendingAudio(AUDIO_CURRENTITEM);
  addPendingAudio(AUDIO_COMPLETETIME);
  // Ensure minimum audio level in menu
  if (g_MP3Volume < MP3MINVOLUME) g_pendingAudio.volume = MP3MINVOLUME;

  // Initial timestamp for user input timeout
  lastUserInputUTC = getCurrentUTC();

  do { // Loop until User input or timeout expiration
    wdt_reset();

    if (currentMenuItem != lastMenuItem) {
      switch(currentMenuItem) {
        case MENUCOMPLETETIME:
          setPendingAudio(AUDIO_COMPLETETIME);
          break;
        case MENUTIMESET:
          setPendingAudio(AUDIO_CHANGETIME);
          break;
        case MENUDCF77:
          setPendingAudio(AUDIO_STARTSYNC);
          break;
        case MENUVOLUME:
          setPendingAudio(AUDIO_CHANGEVOLUME);
          break;
        case MENULANGUAGE:
          setPendingAudio(AUDIO_LANGUAGE);
          addPendingAudio(AUDIO_LANGUAGEEN + g_languageID);
          addPendingAudio(AUDIO_LANGUAGEEDIT);
          break;
        case MENUVCC:
          Vcc = getBandgap();
          setPendingAudio(AUDIO_VCC);
          addPendingAudio(Vcc/100);
          addPendingAudio(AUDIO_DOT);
          addPendingAudio((Vcc%100)/10);
          addPendingAudio(AUDIO_VOLT);
          break;
        case MENUEXIT:
          setPendingAudio(AUDIO_MAINMENUEXIT);
          break;
      }
      lastMenuItem = currentMenuItem;
    }

   relaxedCheckPendingAudio();

    // Go to sleep, when ready
    if (g_rotaryEncoder.readyForSleep()) {
      // We need still millis => Only sleep mode SLEEP_MODE_IDLE is possible
      set_sleep_mode(SLEEP_MODE_IDLE);
      power_spi_disable(); // Disable SPI
      power_usart0_disable(); // Disable the USART 0 module (disables Serial....)
      power_twi_disable(); // Disable I2C, because we do not use I2C devices in this project
      sleep_mode();
    }
    // Change menu item by rotary encoder
    switch (g_rotaryEncoder.getAndResetLastRotation()) {
      case KY040::CLOCKWISE:
        if (currentMenuItem < MAXMENUITEMS - 1) currentMenuItem++; else currentMenuItem = 0;
        lastUserInputUTC = getCurrentUTC();
        beep(MICROBEEP);
        break;
      case KY040::COUNTERCLOCKWISE:
        if (currentMenuItem > 0) currentMenuItem--; else currentMenuItem = MAXMENUITEMS-1;
        lastUserInputUTC = getCurrentUTC();
        beep(MICROBEEP);
        break;
    }

    // Check button press
    switch (g_switchButton.getButton()) {
      case SWITCHBUTTON::SHORTPRESSED:
      case SWITCHBUTTON::LONGPRESSED:
        switch(currentMenuItem) {
          case MENUEXIT:
            exitLoop = true;
            break;
          case MENUVOLUME:
            if (changeVolume()) setPendingAudio(AUDIO_DONE);
            else setPendingAudio(AUDIO_ABORTED);
            addPendingAudio(AUDIO_MAINMENU);
            addPendingAudio(AUDIO_CURRENTITEM);
            addPendingAudio(AUDIO_CHANGEVOLUME);
            // Ensure minimum audio level in menu
            if (g_MP3Volume < MP3MINVOLUME) g_pendingAudio.volume = MP3MINVOLUME; // Use MP3MINVOLUME, when used volume is too low for the menu
            break;
          case MENUCOMPLETETIME:
            sayCompleteTime();
            break;
          case MENULANGUAGE:
            g_languageID ++;
            if (g_languageID >= MAXLANGUAGES) g_languageID = 0;
            setEEPROMLanguage(g_languageID);
            setPendingAudio(AUDIO_LANGUAGE);
            addPendingAudio(AUDIO_LANGUAGEEN + g_languageID);
            break;
          case MENUVCC:
            Vcc = getBandgap();
            setPendingAudio(AUDIO_VCC);
            addPendingAudio(Vcc/100);
            addPendingAudio(AUDIO_DOT);
            addPendingAudio((Vcc%100)/10);
            addPendingAudio(AUDIO_VOLT);
            break;
          case MENUDCF77: // Start DCF77 sync
            g_dcfAudioMode = MP3MODE;
            setTimeFromDCF77();
            g_dcfAudioMode = SILENTMODE;
            currentUTCTime = getCurrentUTC();
            if (!g_weakTime) { // Success
              // Schedule daily DCF77 sync
              g_nextDCF77Sync = tmConvert_t(year(currentUTCTime), month(currentUTCTime), day(currentUTCTime), DCF77SYNCHOUR, 0,0)+SECS_PER_DAY;
              setPendingAudio(AUDIO_SYNCSUCCESS);
            } else setPendingAudio(AUDIO_SYNCABORTED);
            addPendingAudio(AUDIO_MAINMENU);
            addPendingAudio(AUDIO_CURRENTITEM);
            addPendingAudio(AUDIO_STARTSYNC);
            break;
          case MENUTIMESET: // Set time manually
            setManualTime();
            setPendingAudio(AUDIO_MAINMENU);
            addPendingAudio(AUDIO_CURRENTITEM);
            addPendingAudio(AUDIO_CHANGETIME);
            break;
        }
        lastUserInputUTC = getCurrentUTC();
        relaxedCheckPendingAudio();
        break;
    }
    if (getCurrentUTC() - lastUserInputUTC > USERTIMEOUT) { // Exit on timeout
      // Abort menu
      exitLoop = true;
    }
  } while (!exitLoop);

  setPendingAudio(AUDIO_MAINMENUEXITED);
  g_pendingAudio.volume = g_MP3Volume;
}

// Set time and date manually
void setManualTime() {
  // Display modes
  enum mode { SELECT, EDIT, MAXMODES };
  // Selection items
  enum selectItems { SELECTHOUR, SELECTMINUTE, SELECTSECOND, SELECTDAY, SELECTMONTH, SELECTYEAR, SELECTBACK, MAXSELECTITEMS };
  mode currentMode=SELECT;
  mode lastMode = currentMode;
  int currentSelectedItem = SELECTHOUR;
  int currentValue = 0;
  int lastValue = MAXSELECTITEMS;
  int currentMaxOptions = MAXSELECTITEMS; // Encoder limit
  // Days of all month
  byte monthLengthList[]={31,28,31,30,31,30,31,31,30,31,30,31};
  byte monthLength;
  unsigned long lastUserInputUTC = 0;
  bool exitLoop = false;
  bool aborted = false;
  time_t localTime;

  // Set initial encoder value
  currentValue = currentSelectedItem;
  lastValue = currentValue;
  lastUserInputUTC = getCurrentUTC();

  // Intro audio
  setPendingAudio(AUDIO_CHANGETIMEDESC);
  addPendingAudio(AUDIO_CURRENTITEM);
  addPendingAudio(AUDIO_HOUR);
  addPendingAudio(hour(localTime));

  do { // Loop until user select OK or cancel or input timeout expiration
    wdt_reset();

    // Say current selection or value if changed
    if (currentValue != lastValue) {
      switch (currentMode) {
        case SELECT: // Item selection
          localTime = UTCtoLocalTime(getCurrentUTC());
          if (aborted) {
            setPendingAudio(AUDIO_ABORTED);
            aborted = false;
          } else clearPendingAudio();
          switch (currentValue) {
            case SELECTHOUR:
              setPendingAudio(AUDIO_HOUR);
              addPendingAudio(hour(localTime));
              break;
            case SELECTMINUTE:
              setPendingAudio(AUDIO_MINUTE);
              addPendingAudio(minute(localTime));
              break;
            case SELECTSECOND:
              setPendingAudio(AUDIO_SECOND);
              addPendingAudio(second(localTime));
              break;
            case SELECTDAY:
              setPendingAudio(AUDIO_DAY);
              addPendingAudio(day(localTime));
              break;
            case SELECTMONTH:
              setPendingAudio(AUDIO_MONTH);
              addPendingAudio(month(localTime));
              break;
            case SELECTYEAR:
              setPendingAudio(AUDIO_YEAR);
              addPendingAudio(AUDIO_1970+year(localTime)-1970);
              break;
            case SELECTBACK:
              setPendingAudio(AUDIO_BACK);
              break;
          }
          break;
        case EDIT: // Value changes
          switch (currentSelectedItem) {
            case SELECTHOUR:
            case SELECTMINUTE:
            case SELECTSECOND:
            case SELECTDAY:
            case SELECTMONTH:
              setPendingAudio(currentValue);
              if (lastMode != currentMode) addPendingAudio(AUDIO_EDITTIME);
              break;
            case SELECTYEAR:
              setPendingAudio(AUDIO_1970+currentValue);
              if (lastMode != currentMode) addPendingAudio(AUDIO_EDITTIME);
              break;
          }
          break;
      }
      lastValue = currentValue;
    }
    lastMode = currentMode;
    relaxedCheckPendingAudio();

    // Go to sleep, when ready
    if (g_rotaryEncoder.readyForSleep()) {
      // We need still millis => Only sleep mode SLEEP_MODE_IDLE is possible
      set_sleep_mode(SLEEP_MODE_IDLE);
      power_spi_disable(); // Disable SPI
      power_usart0_disable(); // Disable the USART 0 module (disables Serial....)
      power_twi_disable(); // Disable I2C, because we do not use I2C devices in this project
      sleep_mode();
    }

    // Check button rotation
    switch (g_rotaryEncoder.getAndResetLastRotation()) {
      case KY040::CLOCKWISE:
        if (currentValue < currentMaxOptions - 1) currentValue++; else currentValue = 0;
        lastUserInputUTC = getCurrentUTC();
        beep(MICROBEEP);
        break;
      case KY040::COUNTERCLOCKWISE:
        if (currentValue > 0) currentValue--; else currentValue = currentMaxOptions -1;
        lastUserInputUTC = getCurrentUTC();
        beep(MICROBEEP);
        break;
    }

    // Use encoder value for item selection or value changes
    switch (currentMode) {
      case SELECT: // Item selection
        currentSelectedItem = currentValue;
        break;
      case EDIT: // Value changes
        switch (currentSelectedItem) {
          case SELECTHOUR:
            localTime = tmConvert_t(year(localTime), month(localTime), day(localTime), currentValue, minute(localTime), second(localTime));
            break;
          case SELECTMINUTE:
            localTime = tmConvert_t(year(localTime), month(localTime), day(localTime), hour(localTime), currentValue, second(localTime));
            break;
          case SELECTSECOND:
            localTime = tmConvert_t(year(localTime), month(localTime), day(localTime), hour(localTime), minute(localTime), currentValue);
            break;
          case SELECTDAY:
            localTime = tmConvert_t(year(localTime), month(localTime), currentValue+1, hour(localTime), minute(localTime), second(localTime));
            break;
          case SELECTMONTH:
            localTime = tmConvert_t(year(localTime), currentValue+1, day(localTime), hour(localTime), minute(localTime), second(localTime));
            break;
          case SELECTYEAR:
            localTime = tmConvert_t(currentValue+1970, month(localTime), day(localTime), hour(localTime), minute(localTime), second(localTime));
            break;
        }
        break;
    }

    // Check button press
    switch (g_switchButton.getButton()) {
      case SWITCHBUTTON::SHORTPRESSED:
      case SWITCHBUTTON::LONGPRESSED:
        lastUserInputUTC = getCurrentUTC();
        switch (currentMode) {
          case SELECT:
            switch(currentSelectedItem) {
              case SELECTHOUR: // Change mode to edit hour
                currentMode = EDIT;
                currentMaxOptions = 24;
                currentValue=hour(localTime);
                lastValue = currentMaxOptions;
                break;
              case SELECTMINUTE: // Change mode to edit minute
                currentMode = EDIT;
                currentMaxOptions = 60;
                currentValue=minute(localTime);
                lastValue = currentMaxOptions;
                break;
              case SELECTSECOND: // Change mode to edit second
                currentMode = EDIT;
                currentMaxOptions = 60;
                currentValue=second(localTime);
                lastValue = currentMaxOptions;
              break;                                                                                                          break;
              case SELECTDAY: // Change mode to edit day
                monthLength =  monthLengthList[month(localTime)-1];
                if (year(localTime) % 4 == 0) monthLength++; // Leap year
                currentMode = EDIT;
                currentMaxOptions = monthLength;
                currentValue = day(localTime)-1;
                lastValue = currentMaxOptions;
                break;
              case SELECTMONTH: // Change mode to edit month
                currentMode = EDIT;
                currentMaxOptions = 12;
                currentValue = month(localTime)-1;
                lastValue = currentMaxOptions;
                break;
              case SELECTYEAR: // Change mode to edit year
                currentMode = EDIT;
                currentMaxOptions = 65;
                currentValue = year(localTime)-1970;
                lastValue = currentMaxOptions;
                break;
              case SELECTBACK: // Back
                exitLoop = true;
                break;
            }
            break;
          case EDIT: // Change mode to select items
            currentMaxOptions = MAXSELECTITEMS;
            currentValue = currentSelectedItem;
            lastValue = currentValue;
            currentMode = SELECT;
            setCurrentUTC(localTimeToUTC(localTime));
            g_initTimeSyncPending = false;
            g_weakTime = true; // Do not 100% trust manual time
            lastUserInputUTC = getCurrentUTC();
            // Schedule next DCF77 sync for next day
            g_nextDCF77Sync = tmConvert_t(year(lastUserInputUTC), month(lastUserInputUTC), day(lastUserInputUTC), DCF77SYNCHOUR, 0,0)+SECS_PER_DAY;
            // Say current selected item
            localTime = UTCtoLocalTime(getCurrentUTC());
            setPendingAudio(AUDIO_CURRENTITEM);
            switch (currentValue) {
              case SELECTHOUR:
                addPendingAudio(AUDIO_HOUR);
                addPendingAudio(hour(localTime));
                break;
              case SELECTMINUTE:
                addPendingAudio(AUDIO_MINUTE);
                addPendingAudio(minute(localTime));
                break;
              case SELECTSECOND:
                addPendingAudio(AUDIO_SECOND);
                addPendingAudio(second(localTime));
                break;
              case SELECTDAY:
                addPendingAudio(AUDIO_DAY);
                addPendingAudio(day(localTime));
                break;
              case SELECTMONTH:
                addPendingAudio(AUDIO_MONTH);
                addPendingAudio(month(localTime));
                break;
              case SELECTYEAR:
                addPendingAudio(AUDIO_YEAR);
                addPendingAudio(AUDIO_1970+year(localTime)-1970);
                break;
            }
           break;
        }
        break;
    }

    if (getCurrentUTC() - lastUserInputUTC > USERTIMEOUT) { // Exit on timeout
      switch (currentMode) {
        case SELECT:
          // Abort selection
          exitLoop = true;
          break;
        case EDIT:
          // Go back to selection
          currentMaxOptions = MAXSELECTITEMS;
          currentValue = currentSelectedItem;
          currentMode = SELECT;
          lastValue = MAXSELECTITEMS;
          lastUserInputUTC = getCurrentUTC();
          aborted = true;
          break;
      }
    }
  } while (!exitLoop);
}

// Change audio volume level
bool changeVolume() {
  #define MAXSTRDATALENGTH 255
  char strData[MAXSTRDATALENGTH+1];
  int currentValue = g_MP3Volume;
  int lastValue = currentValue;
  int currentMaxValue = 31; // Encoder limit
  unsigned long lastUserInputUTC = 0;
  bool exitLoop = false;
  bool aborted = false;
  unsigned long lastAudioChangeMS = 0;

  // Intro audio
  setPendingAudio(AUDIO_VOLUME);
  addPendingAudio(currentValue);
  addPendingAudio(AUDIO_EDITVOLUME);
  // Ensure minimum audio level for intro
  if (g_MP3Volume < MP3MINVOLUME) g_pendingAudio.volume = MP3MINVOLUME;

  // Initial timestamp for user input timeout
  lastUserInputUTC = getCurrentUTC();

  do { // Loop until User select OK or cancel or input timeout expiration
    wdt_reset();

    if (currentValue != lastValue) {
      setPendingAudio(currentValue);
      g_pendingAudio.volume = currentValue;
      lastValue=currentValue;
    }

    relaxedCheckPendingAudio();

    // Go to sleep, when ready
    if (g_rotaryEncoder.readyForSleep()) {
      // We need still millis => Only sleep mode SLEEP_MODE_IDLE is possible
      set_sleep_mode(SLEEP_MODE_IDLE);
      power_spi_disable(); // Disable SPI
      power_usart0_disable(); // Disable the USART 0 module (disables Serial....)
      power_twi_disable(); // Disable I2C, because we do not use I2C devices in this project
      sleep_mode();
    }

    // Check button rotation
    switch (g_rotaryEncoder.getAndResetLastRotation()) {
      case KY040::CLOCKWISE:
        if (currentValue < currentMaxValue - 1) currentValue++;
        lastUserInputUTC = getCurrentUTC();
        beep(MICROBEEP);
        break;
      case KY040::COUNTERCLOCKWISE:
        if (currentValue > 0) currentValue--;
        lastUserInputUTC = getCurrentUTC();
        beep(MICROBEEP);
        break;
    }

    // Check button press
    switch (g_switchButton.getButton()) {
      case SWITCHBUTTON::SHORTPRESSED:
      case SWITCHBUTTON::LONGPRESSED:
        g_MP3Volume = currentValue;
        setEEPROMMP3Volume(g_MP3Volume);
        exitLoop = true;
        break;
    }

    if (getCurrentUTC() - lastUserInputUTC > USERTIMEOUT) { // Exit on timeout expiration
      exitLoop = true;
      aborted = true;
      break;
    }
  } while (!exitLoop);

  g_pendingAudio.volume = g_MP3Volume;
  return !aborted;
}

// Say time
void sayTime(unsigned long clock) {
  MP3On(); // Enable audio module if not already enabled

  switch (g_languageID) {
    case EN: // EN time format
      addPendingAudio(hour(clock));
      if (minute(clock) == 0) addPendingAudio(AUDIO_OCLOCK); else {
        if (minute(clock) < 10) addPendingAudio(AUDIO_01+minute(clock)-1);
        else addPendingAudio(minute(clock));
      }
      break;
    case DE: // DE time format
      byte audioHour = hour(clock);
      if (audioHour == 1) audioHour=AUDIO_ONE;
      addPendingAudio(audioHour);
      addPendingAudio(AUDIO_CLOCK);
      addPendingAudio(minute(clock));
      break;
  }
}
