/*
 * Project: DCF77voiceClock (Ding21)
 *
 * Description:
 * "The DCF77voiceClock is on my bedside table and if I want to know the 
 * time at night without opening my eyes or saying anything, all I have to 
 * do is put my hand on or over the DCF77voiceClock and the time is 
 * announced to me."
 * 
 * The DCF77voiceClock is a DIY clock without a display and speaks the 
 * current time acoustically when a motion is detected by a PIR sensor
 * - Device works offline (audio samples are stored locally on a MP3 module)
 * - Time can be synced by DCF77 (manually and once per day at 00:00)
 * - Time can be set manually
 * - Device runs on battery
 * - Device has no display and communicates only acoustically (also in menu or when changing settings)
 * - Audio samples for languages EN and DE (samples were created by powershell on a Windows 11 computer)
 *
 * License: 2-Clause BSD License
 * Copyright (c) 2024 codingABI
 * For details see: License.txt
 *
 * created by codingABI https://github.com/codingABI/DCF77voiceClock
 *
 * External code:
 * I use external code in this project in form of libraries and two small
 * code piece called summertime_EU and getBandgap, but does not provide these code.
 * 
 * If you want to compile my project, you should be able to download the
 * needed libraries:
 * - Dusk2Dawn (by DM Kishi)
 * - DCF77 (by Thijs Elenbaas)
 * - Time (by Michael Margolis/Paul Stoffregen)
 * with the Arduino IDE Library Manager and the libraries:
 * - DFR0534 (https://github.com/codingABI/DFR0534 by codingABI)
 * - SWITCHBUTTON (https://github.com/codingABI/SWITCHBUTTON by codingABI)
 * - KY040 (https://github.com/codingABI/KY040 by codingABI)
 * from github.
 * 
 * For details to get the small code pieces for
 * - summertime_EU "European Daylight Savings Time calculation by "jurs" for German Arduino Forum"
 * - getBandgap by "Coding Badly" and "Retrolefty" from the Arduino forum
 * see externalCode.ino.
 *
 * Hardware:
 * - Microcontroller ATmega328P (In 8 MHz-RC mode. Board manager: "ATmega328 on a breadboard (8 MHz internal clock)" )
 * - 32768 kHZ clock crystal for timer2 (Without a DCF77 sync time drifts ~8s per day)
 * - DCF-3850M-800 DCF77 time signal receiver
 * - Speaker 2 Watt, 8 Ohm
 * - AM312 PIR sensor
 * - DFR0534 audio module
 * - IRLZ44NPBF FET to power on/off the audio module
 * - 2xIRLML2502 FETs to disconnect speaker during audio module startup (avoid startup noise)
 * - Passive buzzer for simple audio signals
 * - KY-040 rotary encoder
 * - HT7333A 3.3V voltage regulator
 * - 3.7V 3500mA Li-Ion battery with a TC4056 as loader and protection
 *
 * Power consumption
 * - While waiting for a motion detection ~0.3mA@3.3V
 * - In main menu, volume control or time announcement 35-200mA@3.3V
 * - While daily DCF77 synchronisation ~2mA@3.3V (max for 15 minutes)
 *
 * Buzzer codes
 * - Flash                     = When device is powered on
 * - Micro clicks              = When turning the knob or when receiving a DCF77 signal
 * - 1x Short                  = Main menu started
 * - 2x Short                  = Vcc voltage too high
 * - 3x Short every 15 minutes = Low battery
 *
 * Notes
 * - The audio module DFR0534 consumes a lot of power (up to ~2W) and will be switched off when not needed
 * - During the menu system, the audio module DFR0534 is needed => Keep you menu actions short
 * - Do not connect the DFR0534 module to USB without removing the DFR0534 module from the 
 *   perfboard because the DFR0534 module will connect 5V from USB to the TP4056 output 
 *   and can break the TP4056 or other components 
 * - Do not connect ICSP-Vcc other than 3.3V (Better leave ICSP-Vcc unconnected)
 * - When you get compile error "multiple definition of `__vector_5'"
 *   comment out "ISR(PCINT2_vect, ISR_ALIASOF(PCINT0_vect));" in
 *   ...portable\packages\arduino\hardware\avr\1.8.6\libraries\SoftwareSerial\src\SoftwareSerial.cpp
 *   
 * History: 
 * 20240208, Initial version  
 * 20240321, Add g_initTimeSyncPending
 * 20240511, Remote unneeded defines
 */
 
#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/power.h> //Needed for powering down peripherals such as the ADC/TWI and Timers
#include <TimeLib.h>
#include <avr/wdt.h>
#include <DCF77.h>
#include <EEPROM.h>
// #include <NeoSWSerial.h> // NeoSWSerial would not work because it uses timer2 when ATmega328 runs in 8 MHz mode
#include <SoftwareSerial.h>
#include <KY040.h>
#include <SWITCHBUTTON.h>
#include <DFR0534.h>
#include <Dusk2Dawn.h>
#include "audioSamples.h"
#include "secrets.h"

// Pin definitions
#define PIR_PIN 3 // PIR data
#define MP3TX_PIN A0 // MP3 module TX
#define MP3RX_PIN A1 // MP3 module RX
#define MP3VCC_PIN A2 // MP3 moduleVcc control via FET
#define MP3MUTE_PIN 8 // MP3 module speaker control via FET
#define BUZZER_PIN 9 // Passive buzzer
#define ROTARY_DT_PIN 5 // Rotary encoder DT
#define ROTARY_CLK_PIN 6 // Rotary encoder CLK
#define ROTARY_SW_PIN 4 // Rotary encoder SW button
#define DCF77_VCC_PIN 10 // DCF77 Vcc
#define DCF77_OUT_PIN 2 // DCF77 out

#define USERTIMEOUT 60 // Set timeout in MS for user inputs
#define DCF77SYNCHOUR 0 // Hour for daily DCF77 time sync
#define MP3INITVOLUME 20 // Default audio level
#define MP3MINVOLUME 10 // Minium audio in menu
#define MAXIDLECHECKS 2 // Number of consecutive IDLE checks before audio module will be powered of in loop
#define LANGUAGEINITID EN // Default speech language

/* Vcc levels
 * Vcc for the MCU is provided by a HT7333A 3.3V voltage regulator.
 * => If MCU is falling below 3.3V the battery voltage is below 3.36V
 */

// Below this Vcc value no time sync will be startet and device will beep tree times every 15 minutes
#define LOWBAT10MV_3V0 300
// Over this Vcc value automatic DCF77 syncs and MP3 module will be enabled
#define FULLBAT10MV_3V2 320
/* Vss over 3.3V is not recommend, but could be possible, if the user switch off the device and
 * powers the MCU by the ICSP-Vcc pin. This could damaged 3.3V devices like the MP3 module.
 * => When Vcc >= OVERBAT10MV_4V0 the device will stop, beeps twice and resets
 */
#define OVERBAT10MV_4V0 400

// -------- Global variables ------------

volatile unsigned long v_clock = 0; // Timer2 driven system clock

KY040 g_rotaryEncoder(ROTARY_CLK_PIN,ROTARY_DT_PIN); // Rotary encoder
SWITCHBUTTON g_switchButton(ROTARY_SW_PIN); // Rotary encoder switch button
DCF77 DCF = DCF77(DCF77_OUT_PIN,digitalPinToInterrupt(DCF77_OUT_PIN)); // DCF77 module
bool g_weakTime = true; // True, if no DCF77 sync was startet or did not succeed
unsigned long g_nextDCF77Sync = 0; // Next planed DCF77 sync
SoftwareSerial* g_MP3Serial; // Serial connection to MP3 module
DFR0534 *g_audio; // Audio module
bool g_MP3enabled = false; // True, if MP3 module was enabled
byte g_MP3Volume = MP3INITVOLUME; // Current audio level (0-30)
bool g_MP3enablePending = false; // True, when the audio module was powered up, but is not yet ready for playing audio
bool g_initTimeSyncPending = true; // True, when time was never synced
unsigned long g_MP3powerOnMS = 0; // Millis timestamp, when the audio module was powered up
volatile bool v_PIRAlert = false; // True, if PIR alerts
enum DCFAUDIOMODES { INITMODE, SILENTMODE, MP3MODE }; // Audio modes during DCF77 sync
byte g_dcfAudioMode = INITMODE; // Active audio mode during DCF77 sync
enum beepTypes { // Beep types for the buzzer
  DEFAULTBEEP,
  SHORTBEEP,
  LONGBEEP,
  HIGHSHORTBEEP,
  LASER,
  MICROBEEP };
enum LANGUAGES { EN, DE, MAXLANGUAGES }; // Speech languages
byte g_languageID; // Current speech language

// Pending audio sequence and volume level
#define MAXPENDINGAUDIOLENGTH 255
struct {
  char sequence[MAXPENDINGAUDIOLENGTH+1] = "";
  byte volume = 0;
} g_pendingAudio;

// ISR for PIR
void ISR_PIR() {
  v_PIRAlert = true;
}

// ISR for Timer2 overflow
ISR(TIMER2_OVF_vect) {
  v_clock++;
}

// Initialize Timer2 as asynchronous 32768 Hz timing source
void timer2_init(void) {
  TCCR2B = 0; //stop Timer 2
  TIMSK2 = 0; // disable Timer 2 interrupts
  ASSR = (1 << AS2); // select asynchronous operation of Timer2
  TCNT2 = 0; // clear Timer 2 counter
  TCCR2A = 0; //normal count up mode, no port output
  TCCR2B = (1 << CS22) | (1 << CS20); // select prescaler 128 => 1 sec between each overflow

  while (ASSR & ((1 << TCN2UB) | (1 << TCR2BUB))); // wait for TCN2UB and TCR2BUB to be cleared

  TIFR2 = (1 << TOV2); // clear interrupt-flag
  TIMSK2 = (1 << TOIE2); // enable Timer2 overflow interrupt
}

// Enable WatchdogTimer
void enableWatchdogTimer() {
  /*
   * From Atmel datasheet: "...WDTCSR – Watchdog Timer Control Register...
   * Bit
   * 7 WDIF
   * 6 WDIE
   * 5 WDP3
   * 4 WDCE
   * 3 WDE
   * 2 WDP2
   * 1 WDP1
   * 0 WDP0
   * ...
   * DP3 WDP2 WDP1 WDP0 Number of WDT Oscillator Cycles Typical Time-out at VCC = 5.0V
   * 0 0 0 0 2K (2048) cycles 16ms
   * 0 0 0 1 4K (4096) cycles 32ms
   * 0 0 1 0 8K (8192) cycles 64ms
   * 0 0 1 1 16K (16384) cycles 0.125s
   * 0 1 0 0 32K (32768) cycles 0.25s
   * 0 1 0 1 64K (65536) cycles 0.5s
   * 0 1 1 0 128K (131072) cycles 1.0s
   * 0 1 1 1 256K (262144) cycles 2.0s
   * 1 0 0 0 512K (524288) cycles 4.0s
   * 1 0 0 1 1024K (1048576) cycles 8.0s"
   */
  cli();
  // Set bit 3+4 (WDE+WDCE bits)
  // From Atmel datasheet: "...Within the next four clock cycles, write the WDE and
  // watchdog prescaler bits (WDP) as desired, but with the WDCE bit cleared.
  // This must be done in one operation..."
  WDTCSR = WDTCSR | B00011000;
  // Set Watchdog-Timer duration to 8 seconds
  WDTCSR = B00100001;
  // Enable Watchdog interrupt by WDIE bit and enable device reset via 1 in WDE bit.
  // Frosetm Atmel datasheet: "...The third mode, Interrupt and system reset mode, combines the other two modes by first giving an interrupt and then switch to system reset mode. This mode will for instance allow a safe shutdown by saving critical parameters before a system reset..."
  WDTCSR = WDTCSR | B01001000;
  sei();
}

// ISR for the watchdog timer
ISR(WDT_vect) {
  // Set buzzer pin to HIGH to show the pending WDT reset 
  digitalWrite(BUZZER_PIN,HIGH);

  // In 8 seconds device will reset
  while(true);
}

// Enable pin change interrupt
void pciSetup(byte pin) {
  *digitalPinToPCMSK(pin) |= bit (digitalPinToPCMSKbit(pin));  // enable pin
  PCIFR  |= bit (digitalPinToPCICRbit(pin)); // clear any outstanding interrupt
  PCICR  |= bit (digitalPinToPCICRbit(pin)); // enable interrupt for the group
}

// ISR to handle pin change interrupt for D0 to D7
ISR (PCINT2_vect) {
  byte pins = PIND;
  g_rotaryEncoder.setState((pins & 0b01100000)>>5);
  g_rotaryEncoder.checkRotation();
}

// Buzzer
void beep(byte type) {
  pinMode(BUZZER_PIN, OUTPUT);
  switch(type) {
    case DEFAULTBEEP: { // 500 Hz for 200ms
      for (int i=0;i < 100;i++) {
        digitalWrite(BUZZER_PIN,HIGH);
        delay(1);
        digitalWrite(BUZZER_PIN,LOW);
        delay(1);
      }
      break;
    }
    case MICROBEEP: { // 1 kHz for 2ms
      for (int i=0;i < 2;i++) {
        digitalWrite(BUZZER_PIN,HIGH);
        delayMicroseconds(500);
        digitalWrite(BUZZER_PIN,LOW);
        delayMicroseconds(500);
      }
      break;
    }
    case SHORTBEEP: { // 1 kHz for 100ms
      for (int i=0;i < 100;i++) {
        digitalWrite(BUZZER_PIN,HIGH);
        delayMicroseconds(500);
        digitalWrite(BUZZER_PIN,LOW);
        delayMicroseconds(500);
      }
      break;
    }
    case LONGBEEP: { // 250 Hz for 400ms
      for (int i=0;i < 100;i++) {
        digitalWrite(BUZZER_PIN,HIGH);
        delay(2);
        digitalWrite(BUZZER_PIN,LOW);
        delay(2);
      }
      break;
    }
    case HIGHSHORTBEEP: { // 5 kHz for 100ms
      for (int i=0;i < 500;i++) {
        digitalWrite(BUZZER_PIN,HIGH);
        delayMicroseconds(100);
        digitalWrite(BUZZER_PIN,LOW);
        delayMicroseconds(100);
      }
      break;
    }
    case LASER: { // Laser like sound
      int i = 5000; // Start frequency in Hz (goes down to 300 Hz)
      int j = 150; // Start duration in microseconds (goes up to 5000 microseconds)
      while (i>300) {
        i -=50;
        j +=50;
        for (int k=0;k < j/(1000000/i);k++) {
          digitalWrite(BUZZER_PIN,HIGH);
          delayMicroseconds(500000/i);
          digitalWrite(BUZZER_PIN,LOW);
          delayMicroseconds(500000/i);
        }
        delayMicroseconds(1000);
      }
      break;
    }
  }
  pinMode(BUZZER_PIN, INPUT);
  delay(50);
  wdt_reset();
}

// Set clock to seconds since 1.1.1970
void setCurrentUTC(unsigned long clock) {
  cli();
  v_clock = clock;
  sei();
}

// Return clock in seconds since 1.1.1970
unsigned long getCurrentUTC() {
  cli();
  unsigned long clock = v_clock;
  sei();
  return clock;
}

// Create time_t from time components
time_t tmConvert_t(int YYYY, byte MM, byte DD, byte hh, byte mm, byte ss)
{
  tmElements_t tmSet;
  tmSet.Year = YYYY - 1970;
  tmSet.Month = MM;
  tmSet.Day = DD;
  tmSet.Hour = hh;
  tmSet.Minute = mm;
  tmSet.Second = ss;
  return makeTime(tmSet);
}

// Convert local time to UTC
time_t localTimeToUTC(time_t localTime) {
  if (summertime_EU(year(localTime),month(localTime),day(localTime),hour(localTime),1)) {
    return localTime-7200UL; // Summer time (Germany)
  } else {
    return localTime-3600UL; // Winter time (Germany)
  }
}

// Convert UTC to local time
time_t UTCtoLocalTime(time_t UTC) {
  if (summertime_EU(year(UTC),month(UTC),day(UTC),hour(UTC),0)) {
    return UTC+7200UL; // Summer time (Germany)
  } else {
    return UTC+3600UL; // Winter time (Germany)
  }
}

void setup() {
  enableWatchdogTimer(); // Watchdog timer (Start at the begin of setup to prevent a boot loop after a WDT reset)

  beep(LASER); // Startup sound

  // Enable timer2 for clock
  timer2_init();
  // Pin change interrupts for buttons
  pciSetup(ROTARY_SW_PIN);
  pciSetup(ROTARY_CLK_PIN);
  pciSetup(ROTARY_DT_PIN);

  if (!checkEEPROMHeader()) writeEEPROMHeader();
  // Get settings from EEPROM
  g_MP3Volume = getEEPROMMP3Volume();
  g_languageID = getEEPROMLanguage();

  // Interrupt for PIR sensor
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), ISR_PIR, RISING);

  // Startup sound
  #define STARTUPSOUND "/STARTUP MP3"
  #ifdef STARTUPSOUND
  MP3On();
  snprintf(g_pendingAudio.sequence,MAXPENDINGAUDIOLENGTH+1,"%s",STARTUPSOUND);
  g_pendingAudio.volume = g_MP3Volume;
  checkPendingAudio();
  #endif
}

void loop() {
  unsigned long currentUTCTime;
  static unsigned long lastVccUTCTime = 0;
  #define VCCUNKNOWN -1000
  static int Vcc = VCCUNKNOWN;
  bool VccReady = false;
  bool PIRAlert;
  static unsigned long lastAudioCheckUTC = 0;
  static byte idleCounter;

  wdt_reset();

  // Update Vcc measurement dependent on previous Vcc
  if (Vcc == VCCUNKNOWN) { // First Vcc measurement needed
    VccReady = true;
  }
  // Increase Vcc measurement intervals when Vcc is low
  if ((Vcc <= LOWBAT10MV_3V0) && (currentUTCTime - lastVccUTCTime > 60)) VccReady = true;
  if ((Vcc > LOWBAT10MV_3V0) && (currentUTCTime - lastVccUTCTime > 10)) VccReady = true;

  if (VccReady) {
    Vcc = getBandgap(); // Every getBandgap costs 50ms full speed runtime
    lastVccUTCTime = currentUTCTime;
  }

  if (Vcc >= OVERBAT10MV_4V0) { // Vcc to high for audio module
    beep(SHORTBEEP);
    beep(SHORTBEEP);
    MP3Off();
    while(true); // Waits for watchdog reset
  }

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

  checkPendingAudio();

  if (Vcc > LOWBAT10MV_3V0) { // Vcc ready for DCF77, audio ...?
    // Ready for initial DCF77 sync?
    if ((Vcc > FULLBAT10MV_3V2) && (currentUTCTime>g_nextDCF77Sync)) {
      // Get time from DCF77
      setTimeFromDCF77();
      if (g_dcfAudioMode == INITMODE) {
        MP3On();
        if (!g_weakTime) setPendingAudio(AUDIO_SYNCSUCCESS);
        else setPendingAudio(AUDIO_SYNCABORTED);
        g_dcfAudioMode = SILENTMODE;
      }
      cli();
      v_PIRAlert = false; // Clear unprocessed PIR interrupt
      sei();
      currentUTCTime = getCurrentUTC();
      if (g_initTimeSyncPending) { // If time was never set/synced
        // Schedule next DCF77 sync in 4h
        g_nextDCF77Sync = currentUTCTime + 4*SECS_PER_HOUR;
      } else {
        // Schedule daily DCF77 sync 00:00 next day
        g_nextDCF77Sync = tmConvert_t(year(currentUTCTime), month(currentUTCTime), day(currentUTCTime), DCF77SYNCHOUR, 0,0)+SECS_PER_DAY;
      }
    }

    cli();
    PIRAlert = v_PIRAlert;
    sei();

    if (PIRAlert) { // PIR alert
      if (!g_initTimeSyncPending) {
        if (Vcc > FULLBAT10MV_3V2) {
          // Say current time
          clearPendingAudio();
          sayTime(UTCtoLocalTime(currentUTCTime));
        } else {
          beep(LONGBEEP); // LOW Battery warning
        }
      }
      cli();
      v_PIRAlert = false;
      sei();
    }

    // Go to menu by rotary encoder
    byte rotation = g_rotaryEncoder.getAndResetLastRotation();
    switch (rotation) {
      case KY040::CLOCKWISE:
      case KY040::COUNTERCLOCKWISE:
          beep(MICROBEEP);
          MP3On();
          if (g_MP3Volume < MP3MINVOLUME) g_pendingAudio.volume = MP3MINVOLUME; // Use MP3MINVOLUME, when used volume is too low for the menu
          if (changeVolume()) setPendingAudio(AUDIO_DONE);
          else setPendingAudio(AUDIO_ABORTED);
          currentUTCTime = getCurrentUTC();
          lastAudioCheckUTC = currentUTCTime;
          idleCounter = 0;
          cli();
          v_PIRAlert = false;
          sei();
        break;
    }

    // Start menu on button press
    switch (g_switchButton.getButton()) {
      case SWITCHBUTTON::SHORTPRESSED:
      case SWITCHBUTTON::LONGPRESSED:
          menu();
          currentUTCTime = getCurrentUTC();
          lastAudioCheckUTC = currentUTCTime;
          idleCounter = 0;
          cli();
          v_PIRAlert = false;
          sei();
        break;
    }
  } else { // Low battery Vcc <= LOWBAT10MV_3V0
    // Beep ever 15 minutes
    #define ALARMINTERVAL 60*15
    if ((currentUTCTime % ALARMINTERVAL) == 0) {
      beep(SHORTBEEP);
      beep(SHORTBEEP);
      beep(SHORTBEEP);
    }
  }

  // Go to sleep, when ready
  if (g_rotaryEncoder.readyForSleep()) {
    if (g_switchButton.readyForSleep() && !g_MP3enablePending) {
      // Now we need no millis & Co. in this loop => Sleep mode SLEEP_MODE_PWR_SAVE is possible
      set_sleep_mode(SLEEP_MODE_PWR_SAVE);
    } else {
      // We need still millis => Only sleep mode SLEEP_MODE_IDLE is possible
      set_sleep_mode(SLEEP_MODE_IDLE);
    }
    power_spi_disable(); // Disable SPI
    power_usart0_disable(); // Disable the USART 0 module (disables Serial....)
    power_twi_disable(); // Disable I2C, because we do not use I2C devices in this project
    sleep_mode();
  }
}
