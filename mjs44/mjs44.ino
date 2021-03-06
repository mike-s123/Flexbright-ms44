/* 

  MS44 firmware for HexBright FLEX 
  v1.5  Dec 31, 2012
  
  MJS program.
  
  Click = quick (<0.2s) press  (on/off)
  Press = brief (0.2-1s) press (change to next mode)
  Long press = 1-2s press, light will fast blink (set brightness for this mode, when off switch sets)
  Extended = 2+ seconds, light will slow blink (start with this mode)
  
  Click turn on/off, starts with last saved setting. When on, Extended press saves 
    current mode as startup mode. Long press adjusts brightness 
    (hold horizontally, twist, click to set).
    
  There are two mode sets - constant (e.g. different
    brightness levels), and dynamic (e.g. different flashing patterns). To get into 
    the opposite set (constant vs. dynamic), Long press from off.
  
  Constant on set (high uses high current mode):
  Press to cycle through low, moon, medium, and high modes. 
   
  Dynamic set (these always use high current mode):
  Press to cycle through dazzle, blink (2 Hz), beacon 
    (0,1 Hz, blinks red tailcap LED 1 Hz), and variable* modes.
    
  *Variable mode gets bright when the flashlight is held horizontally, dim when vertical.  

  1.5 Added variable mode.
  1.4 Can do settings in all modes, sets switch with startmode.    
  1.3 Accel back off when not used (needed to wait before use). Allow setting dynamic as startmode.
  1.2 Added flashing for setting mode, moved code around for memory efficiency, escape prints
      with DEBUG define. Accel now on all the time (only ~0.25 mA). Glowing charge light.
  1.1 EEPROM signature, turn off accel when not used.
  1.0 Initial
*/
/*
 * BOF preprocessor bug prevent
 * insert me on top of your arduino-code
 */
#define nop() __asm volatile ("nop")
#if 1
nop();
#endif
/*
 * EOF preprocessor bug prevent
*/

//#define DEBUG 1
//#define MEMCHK 1

#include <math.h>
#include <Wire.h>
#include <EEPROM.h>

// Settings

#define OVERTEMP                340
// Accelerometer
#define ACC_ADDRESS             0x4C
#define ACC_REG_XOUT            0
#define ACC_REG_YOUT            1
#define ACC_REG_ZOUT            2
#define ACC_REG_TILT            3
#define ACC_REG_SRST            4
#define ACC_REG_INTS            6
#define ACC_REG_MODE            7
#define ACC_REG_SR              8
// Pin assignments
#define DPIN_RLED_SW            2
#define DPIN_GLED               5
#define DPIN_PWR                8 // to latch power on
#define DPIN_DRV_CURRENT        9 // high or low current mode
#define DPIN_DRV_EN             10 // modulate this for brightness
#define DPIN_ACC_INT            3
#define APIN_TEMP               0
#define APIN_CHARGE             3
// Modes, init modes must be even
#define MODE_OFF                0
#define MODE_L1_INIT            2
#define MODE_L1                 3
#define MODE_L2_INIT            4
#define MODE_L2                 5
#define MODE_L3_INIT            6
#define MODE_L3                 7
#define MODE_HIGH_INIT          8
#define MODE_HIGH               9
#define SEQ_DYN_START           10  //first dynamic mode
#define MODE_DAZZLE_INIT        10
#define MODE_DAZZLE             11
#define MODE_BLINKING_INIT      12
#define MODE_BLINKING           13
#define MODE_BEACON_INIT        14
#define MODE_BEACON             15
#define MODE_VAR_INIT           16
#define MODE_VAR                17
// press lengths in ms
#define PRESS_S                 200  //short press
#define PRESS_L                 1000 //long press
#define PRESS_XL                2000 //extra long press
#define PRESS_RESET             10000
// Default brightness and modes
#define DEF_L1                  64  // 50 lm, 30 hr
#define DEF_L2                  8   // guess - around 10 lm, 240 hr
#define DEF_L3                  127 // 255 is 150 lm, 8 hr
#define DEF_HIGH                255 // 500 lm, 1 hr
#define DEF_DZ                  255
#define DEF_BL                  255
#define DEF_BE                  255
#define DEF_CONSTANT            MODE_L1
#define DEF_DYNAMIC             MODE_DAZZLE
// EEPROM
#define EE_CKSUM                0
#define EE_STARTMODE            511
#define EE_L1                   1
#define EE_L2                   2
#define EE_L3                   3
#define EE_HIGH                 4
#define EE_DZ                   5
#define EE_BL                   6
#define EE_BE                   7
#define EE_SIG1                 509
#define SIG1                    'M'
#define EE_SIG2                 510
#define SIG2                    'S'


#if MEMCHK
int freeRam () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}
#endif

// State
byte mode = MODE_OFF, bright_L1 = DEF_L1, bright_L2 = DEF_L2;
byte bright_L3 = DEF_L3, bright_high = DEF_HIGH, bright_dz = DEF_DZ;
byte bright_bl = DEF_BL, bright_be = DEF_BE, bright_curr;
byte startMode = MODE_L1, newMode;
unsigned long btnTime = 0, time, bright_var;
boolean btnDown, btnDownBefore;

void setup()
{
  // We just powered on!  That means either we got plugged 
  // into USB, or the user is pressing the power button.
  
  pinMode(DPIN_PWR,      INPUT);
  digitalWrite(DPIN_PWR, LOW);

  // Initialize GPIO
  pinMode(DPIN_RLED_SW,  INPUT);
  pinMode(DPIN_GLED,     OUTPUT);
  pinMode(DPIN_DRV_CURRENT, OUTPUT);
  pinMode(DPIN_DRV_EN,   OUTPUT);
  digitalWrite(DPIN_DRV_CURRENT, LOW);
  digitalWrite(DPIN_DRV_EN,   LOW);
  
  // Initialize serial busses
#if MEMCHK
Serial.begin(9600);
#endif
// #if DEBUG
Serial.begin(9600);
// #endif

  Wire.begin();
  
  btnTime = millis();
  mode = MODE_OFF;
 
 // get saved values from EEPROM
  byte sig1 = EEPROM.read(EE_SIG1);
  byte sig2 = EEPROM.read(EE_SIG2);
  byte eeChk = EEPROM.read(EE_CKSUM);
  byte chk =0;
  for (int i = 1; i < 512; i++) {
    chk = chk + EEPROM.read(i);
  }
  // init eeprom if checksum or signature doesn't match
  if ( eeChk != chk || sig1 != SIG1 || sig2 != SIG2 ) {
    // clear eeprom
    ee_init();
  } else {
    bright_L1 = EEPROM.read(EE_L1);
    bright_L2 = EEPROM.read(EE_L2);
    bright_L3 = EEPROM.read(EE_L3);
    bright_high = EEPROM.read(EE_HIGH);
    bright_dz = EEPROM.read(EE_DZ); 
    bright_bl = EEPROM.read(EE_BL);
    bright_be = EEPROM.read(EE_BE);
    startMode = EEPROM.read(EE_STARTMODE);    
#if DEBUG
    Serial.print("Read EEPROM OK");
#endif
  }    

  // Configure accelerometer
  byte config[] = {
    ACC_REG_INTS,  // First register (see next line)
    0x00,  // no interrupts
//    0xE4,  // Interrupts: shakes, taps
    0x00,  // Mode: not enabled yet
    0x00,  // Sample rate: 120 Hz
    0x0F,  // Tap threshold
    0x10   // Tap debounce samples
  };
  Wire.beginTransmission(ACC_ADDRESS);
  Wire.write(config, sizeof(config));
  Wire.endTransmission();
  // Leave disabled until needed  
// TCCR1B = TCCR1B & 0b11111011;
  mode = MODE_OFF;
  btnDownBefore = false;
  Serial.println("Poweron");
}

void loop()
{
  static unsigned long lastTempTime, lastTime, lastAccTime;
  static int angleY;
  byte bright;
  static boolean seqSwitch=false;
  
  /* Check for mode changes, btnDown is the current button state,
     btnDownBefore is what it was last time through.
     So, 
     (btnDownBefore && btnDown) button still pressed
     (!btnDownBefore && !btnDown) button still not pressed
     (btnDownBefore && !btnDown) button just released
     (!btnDownBefore && btnDown) button just pressed    
  */
  time = millis();
  newMode = mode;
  btnDown = digitalRead(DPIN_RLED_SW);
  
// handle on/off for all modes
  if (mode == MODE_OFF) { // off, turn on?
    if (btnDownBefore && !btnDown ) { 
      // turn on upon release
      newMode = startMode;
    }      
    if (btnDownBefore && btnDown && (time-btnTime)>PRESS_L) { 
      // button being held, switch between constant/dynamic sequences
      seqSwitch = true;
      if (startMode < SEQ_DYN_START) {
        // switch between constant & dynamic
        newMode = DEF_DYNAMIC;
      } else {
        newMode = DEF_CONSTANT;
      }
    }
  } else { // on, so turn off
      if (btnDownBefore && !btnDown && (time-btnTime)<PRESS_S) {
        newMode = MODE_OFF;
      }
  } // end on-off handling
  
  //
  //
  // Main mode handling
  //
  //
  
  if ( btnDownBefore ) {
    // button being held down
    if (!seqSwitch) {
      // flash when ready to set brightness, all modes when
      // button being held down and not switching sequences on power up
      if (btnDown && (time-btnTime)>PRESS_L && (time-btnTime)<PRESS_XL) {
         blinkoff(1); // fast blink - setting bright
      }
      if (btnDown && (time-btnTime)>PRESS_XL && (time-btnTime)<PRESS_RESET) {
         blinkoff(1);
         delay(250); // slow blink - setting startmode
      }
    switch (mode) {      
      case MODE_L1_INIT:
        // This mode exists just to ignore this button release.
        if (!btnDown) {
          newMode = MODE_L1;
        }
      break;  
      case MODE_L1:
        if (!btnDown && (time-btnTime)>PRESS_S-1 && (time-btnTime)<PRESS_L) {
          newMode = MODE_L2;
        }  
        if (!btnDown && (time-btnTime)>PRESS_L-1 && (time-btnTime)<PRESS_XL ) {
          bright_L1 = setBright(8,255);
          newMode = MODE_L1;
          setCleanup(EE_L1,bright_L1);
        }
        if (!btnDown && (time-btnTime)>PRESS_XL-1 ) {
          setStartmode(MODE_L1);
        }
      break;
    
      case MODE_L2_INIT:
        // This mode exists just to ignore this button release.
        if (!btnDown) {
          newMode = MODE_L2;
        }
      break;  
      case MODE_L2:
        if (!btnDown && (time-btnTime)>PRESS_S && (time-btnTime)<PRESS_L) {
          newMode = MODE_L3;
        }  
        if (!btnDown && (time-btnTime)>PRESS_L-1 && (time-btnTime)<PRESS_XL) {
          bright_L2 = setBright(4,12);
          newMode = MODE_L2;
          setCleanup(EE_L2,bright_L2);
        }
        if (!btnDown && (time-btnTime)>PRESS_XL-1 ) {
          setStartmode(MODE_L2);
        }
      break;   
    
      case MODE_L3_INIT:
        // This mode exists just to ignore this button release.
        if (!btnDown) {
          newMode = MODE_L3;
        }
      break;  
      case MODE_L3:
        if (!btnDown && (time-btnTime)>PRESS_S-1 && (time-btnTime)<PRESS_L) {
          newMode = MODE_HIGH;
        }  
        if (!btnDown && (time-btnTime)>PRESS_L-1 && (time-btnTime)<PRESS_XL) {
          bright_L3 = setBright(64,255);
          newMode = MODE_L3;
          setCleanup(EE_L3,bright_L3);
        }
        if (!btnDown && (time-btnTime)>PRESS_XL-1 ) {
          setStartmode(MODE_L3);
        }
      break;
    
      case MODE_HIGH_INIT:
        // This mode exists just to ignore this button release.
        if (!btnDown) {
          newMode = MODE_HIGH;
        }
        if (!btnDown && (time-btnTime)>PRESS_XL ) {
          setStartmode(MODE_L3);
        }
      break;  
      case MODE_HIGH:
        if (!btnDown && (time-btnTime)>PRESS_S-1 && (time-btnTime)<PRESS_L) {
          newMode = MODE_L1;      
        }
        if (!btnDown && (time-btnTime)>PRESS_L-1 && (time-btnTime)<PRESS_XL) {
          bright_high = setBright(16,255);
          newMode = MODE_HIGH;
          setCleanup(EE_HIGH,bright_high);
        }
        if (!btnDown && (time-btnTime)>PRESS_XL ) {
          setStartmode(MODE_HIGH);
        }
      break;

//
// blinky modes
//
        
      case MODE_DAZZLE_INIT:
        // This mode exists just to ignore this button release.
        if (!btnDown) {
          newMode = MODE_DAZZLE;
        }
        if (!btnDown && (time-btnTime)>PRESS_L-1 && (time-btnTime)<PRESS_XL) {
//          bright_be = setBright(16,255);
          newMode = MODE_VAR;
//          setCleanup(EE_BE,bright_be);
        }
         if (!btnDown && (time-btnTime)>PRESS_XL ) {
          setStartmode(MODE_VAR);
        }
      break;
      case MODE_DAZZLE:
        if (btnDown && (time-btnTime)>PRESS_S)
          newMode = MODE_BLINKING_INIT;
      break;
        
      case MODE_BLINKING_INIT:
        // This mode exists just to ignore this button release.
        if (!btnDown) {
          newMode = MODE_BLINKING;
        }  
        if (!btnDown && (time-btnTime)>PRESS_L-1 && (time-btnTime)<PRESS_XL) {
          bright_dz = setBright(16,255);
          newMode = MODE_DAZZLE;
          setCleanup(EE_DZ,bright_dz);
        }
        if (!btnDown && (time-btnTime)>PRESS_XL ) { // button held, set startMode to previous
          setStartmode(MODE_DAZZLE);
        }
      break;

      case MODE_BLINKING:
        if (btnDown && (time-btnTime)>PRESS_S)
          newMode = MODE_BEACON_INIT;
      break;
        
      case MODE_BEACON_INIT:
        // This mode exists just to ignore this button release.
        if (!btnDown) {
          newMode = MODE_BEACON;
        }  
        if (!btnDown && (time-btnTime)>PRESS_L-1 && (time-btnTime)<PRESS_XL) {
          bright_bl = setBright(16,255);
          newMode = MODE_BLINKING;
          setCleanup(EE_BL,bright_bl);
        }
        if (!btnDown && (time-btnTime)>PRESS_XL ) {
          setStartmode(MODE_BLINKING);
        }
      break;
      case MODE_BEACON:
        if (btnDown && (time-btnTime)>PRESS_S)
          newMode = MODE_VAR_INIT;
      break;

      case MODE_VAR_INIT:
        // This mode exists just to ignore this button release.
        if (!btnDown) {
          newMode = MODE_VAR;
        }  
        if (!btnDown && (time-btnTime)>PRESS_L-1 && (time-btnTime)<PRESS_XL) {
          bright_be = setBright(16,255);
          newMode = MODE_BEACON;
          setCleanup(EE_BE,bright_be);
        }
        if (!btnDown && (time-btnTime)>PRESS_XL ) {
          setStartmode(MODE_BEACON);
        }
      break;
      case MODE_VAR:
        if (btnDown && (time-btnTime)>PRESS_S)
          newMode = MODE_DAZZLE_INIT;
      break;

      } // switch(mode)
    } // if(!seqSwitch)
  } // if(btnDownBefore)

  //
  // Do dynamic modes
  //
  if ( !btnDown || (time-btnTime)<PRESS_L || seqSwitch) { // dynamic mode when user not doing settings
    switch (mode) {
      case MODE_DAZZLE:
      case MODE_DAZZLE_INIT:
        if (time-lastTime < 15) break;
        lastTime = time;
        digitalWrite(DPIN_DRV_EN, random(5)<1);
      break;
      
      case MODE_BLINKING:
      case MODE_BLINKING_INIT:
        digitalWrite(DPIN_DRV_EN, (time%500)<75);
      break;
  
      case MODE_BEACON:
      case MODE_BEACON_INIT:
        digitalWrite(DPIN_DRV_EN, (time%10000)<50);  
        if (time%1000 < 25) {
          pinMode(DPIN_RLED_SW, OUTPUT);
          digitalWrite(DPIN_RLED_SW, HIGH);
          delay(26);
          digitalWrite(DPIN_RLED_SW, LOW);
          pinMode(DPIN_RLED_SW, INPUT);
        }
      break;
      case MODE_VAR_INIT:
      case MODE_VAR:  
        int i = abs(readAccelAngleY());
        if (i>angleY) angleY++ ;
        if (i<angleY) angleY-- ;
        if (angleY>20) angleY = 20;
        if (angleY<1) angleY = 1;
        analogWrite(DPIN_DRV_EN, map(angleY,20,0,4,255));  
        delay(60);
      break;
    } //switch(mode) - dynamic
  }
  
  // Do the mode transitions
  if (newMode != mode)  { 
    switch (newMode) { 
      case MODE_OFF:
#if DEBUG
        Serial.println("Mode=off");
#endif
        pinMode(DPIN_PWR, OUTPUT);
        digitalWrite(DPIN_PWR, LOW);
        digitalWrite(DPIN_DRV_CURRENT, LOW);
        digitalWrite(DPIN_DRV_EN, LOW);
      break;
      case MODE_L1_INIT:
      case MODE_L1:
#if DEBUG
        Serial.println("Mode=L1");
#endif
        pinMode(DPIN_PWR, OUTPUT);
        digitalWrite(DPIN_PWR, HIGH);
        digitalWrite(DPIN_DRV_CURRENT, LOW);
        analogWrite(DPIN_DRV_EN, bright_L1);
        bright_curr = bright_L1;
      break;      
      case MODE_L2:
#if DEBUG
        Serial.println("Mode=L2");
#endif
        pinMode(DPIN_PWR, OUTPUT);
        digitalWrite(DPIN_PWR, HIGH);
        digitalWrite(DPIN_DRV_CURRENT, LOW);
        analogWrite(DPIN_DRV_EN, bright_L2);
        bright_curr = bright_L2;
      break;
      case MODE_L3:
#if DEBUG      
        Serial.println("Mode=L3");
#endif
        pinMode(DPIN_PWR, OUTPUT);
        digitalWrite(DPIN_PWR, HIGH);
        digitalWrite(DPIN_DRV_CURRENT, LOW);
        analogWrite(DPIN_DRV_EN, bright_L3);
        bright_curr =  bright_L3;
      break;
      case MODE_HIGH:
#if DEBUG      
        Serial.println("Mode=HI");
#endif
        pinMode(DPIN_PWR, OUTPUT);
        digitalWrite(DPIN_PWR, HIGH);
        digitalWrite(DPIN_DRV_CURRENT, HIGH);
        analogWrite(DPIN_DRV_EN, bright_high);
        bright_curr = bright_high;
      break;
      case MODE_DAZZLE:
      case MODE_DAZZLE_INIT:
#if DEBUG      
        Serial.println("Mode=DZ");
#endif
        pinMode(DPIN_PWR, OUTPUT);
        digitalWrite(DPIN_PWR, HIGH);
        analogWrite(DPIN_DRV_CURRENT, bright_dz);
        bright_curr = bright_dz;
      break; 
      case MODE_BLINKING:
      case MODE_BLINKING_INIT:
#if DEBUG      
        Serial.println("Mode=BL");
#endif
        pinMode(DPIN_PWR, OUTPUT);
        digitalWrite(DPIN_PWR, HIGH);
        analogWrite(DPIN_DRV_CURRENT, bright_bl);
        bright_curr = bright_bl;
      break; 
      case MODE_BEACON:
      case MODE_BEACON_INIT:
#if DEBUG      
        Serial.println("Mode=BE");
#endif
        pinMode(DPIN_PWR, OUTPUT);
        digitalWrite(DPIN_PWR, HIGH);
        analogWrite(DPIN_DRV_CURRENT, bright_be);
        bright_curr = bright_be;
      break; 
      case MODE_VAR:
      case MODE_VAR_INIT:
//#if DEBUG      
        Serial.println("Mode=VA");
//#endif
        pinMode(DPIN_PWR, OUTPUT);
        digitalWrite(DPIN_PWR, HIGH);
        digitalWrite(DPIN_DRV_CURRENT, LOW);
        analogWrite(DPIN_DRV_EN, 4);
        bright_curr = 127;
      break; 
    } //switch    
    mode = newMode;
  } // if newMode change

  // Check the state of the charge controller
  int chargeState = analogRead(APIN_CHARGE);
  if (chargeState < 128) { // Low - charging
      analogWrite(DPIN_GLED,int(map(time%2000,0,2000,20,200)));
  }
  else if (chargeState > 768) { // High - charged
    digitalWrite(DPIN_GLED, HIGH);
  }
  else { // Hi-Z - shutdown
    digitalWrite(DPIN_GLED, LOW);    
  }
  
  // Check the temperature sensor
  if (time-lastTempTime > 10000) {
    lastTempTime = time;
    int temperature = analogRead(APIN_TEMP);
#if MEMCHK
    Serial.println("\n[memCheck]");
    Serial.println(freeRam());
#endif
#if DEBUG    
    Serial.print("Temp:");
    Serial.println(temperature);
#endif    
    if (temperature > OVERTEMP && mode != MODE_OFF) {
#if DEBUG      
      Serial.println("Overheat!");
#endif
      digitalWrite(DPIN_DRV_CURRENT, LOW);
      mode = MODE_L1;
    }
  }

  // Check if the accelerometer wants to interrupt
  // not used for now
/*  byte tapped = 0, shaked = 0;
  accelOnOff(true); // make sure it's powered up
  if (!digitalRead(DPIN_ACC_INT)) {
    Wire.beginTransmission(ACC_ADDRESS);
    Wire.write(ACC_REG_TILT);
    Wire.endTransmission(false);       // End, but do not stop!
    Wire.requestFrom(ACC_ADDRESS, 1);  // This one stops.
    byte tilt = Wire.read();
    if (time-lastAccTime > 500) {
      lastAccTime = time;
      tapped = !!(tilt & 0x20);
      shaked = !!(tilt & 0x80);  
      if (tapped) Serial.println("Tap!");
      if (shaked) Serial.println("Shake!");
    }
  } */

  // Periodically pull down the button's pin, since
  // in certain hardware revisions it can float.
  pinMode(DPIN_RLED_SW, OUTPUT);
  pinMode(DPIN_RLED_SW, INPUT);
  // Remember button state so we can detect transitions, 
  // this should be the last thing we do in loop()
  if (btnDown != btnDownBefore) { // state changed
    if (!btnDown) seqSwitch=false;
    btnTime = time;
    btnDownBefore = btnDown;
    delay(50); // debounce
  }
} // loop

void ee_init() {
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i,0);
  }
  EEPROM.write(EE_L1,bright_L1);
  EEPROM.write(EE_L2,bright_L2);
  EEPROM.write(EE_L3,bright_L3);
  EEPROM.write(EE_HIGH,bright_high);
  EEPROM.write(EE_DZ,bright_dz);
  EEPROM.write(EE_BL,bright_bl);
  EEPROM.write(EE_BE,bright_be);
  EEPROM.write(EE_SIG1,SIG1);
  EEPROM.write(EE_SIG2,SIG2);
  EEPROM.write(EE_STARTMODE,MODE_L1);    
  ee_write_cksum();
  Serial.println("Init EEPROM");
}

void ee_write_cksum() {
  byte chk = 0;
  for (int i = 1; i < 512; i++) {
    chk = chk + EEPROM.read(i);
  }
  EEPROM.write(EE_CKSUM,chk);  
}

int readAccelAngleY()
{
  char acc[3];
  readAccel(acc);
  return acc[1];
} 

float readAccelAngleXZ()
{
  char acc[3];
  readAccel(acc);
  return atan2(acc[0], acc[2]);
}

void readAccel(char *acc) {
  accelOnOff(true);
  while (1)
  {
    Wire.beginTransmission(ACC_ADDRESS);
    Wire.write(ACC_REG_XOUT);
    Wire.endTransmission(false);       // End, but do not stop!
    Wire.requestFrom(ACC_ADDRESS, 3);  // This one stops.

    for (int i = 0; i < 3; i++)
    {
      if (!Wire.available())
        continue;
      acc[i] = Wire.read();
      if (acc[i] & 0x40)  // Indicates failed read; redo!
        continue;
      if (acc[i] & 0x20)  // Sign-extend
        acc[i] |= 0xC0;
    }
    break;
  }
  accelOnOff(false);
}

void blinkoff (byte times)
// briefly flash light off
{  for (byte i=0;i<times;i++) {
     analogWrite(DPIN_DRV_EN, 0);
     delay(50);
     analogWrite(DPIN_DRV_EN, bright_curr);
     delay(50);
  }  
}

void accelOnOff(boolean state) {
  static byte OnOff;
  if (state && OnOff) { // already on, add another request
    OnOff++;
    return;
  }
  if (!state && OnOff > 1) { // multiple requestors, reduce by 1
    OnOff--;
    return;
  }
  // first req on or last request off, handle it
  byte enable[] = {ACC_REG_MODE, byte(state)}; 
  Wire.beginTransmission(ACC_ADDRESS);
  Wire.write(enable, sizeof(enable));
  Wire.endTransmission();
  OnOff = byte(state);
  delay(21); // time to wake up, 12+1/data rate
}  

byte setBright (byte bMin, byte bMax) {
  float zAngle, angle, oldAngle, twist;
  byte myBtn, bright, oldbright, range;
#if DEBUG
  Serial.println("setBright");
#endif
  accelOnOff(true);
  myBtn = digitalRead(DPIN_RLED_SW);
  zAngle = readAccelAngleXZ();
  angle = 0;
  oldAngle = 0;
  while (myBtn == digitalRead(DPIN_RLED_SW)) {
    angle = -(readAccelAngleXZ() - zAngle);
    if (angle >  PI) angle -= 2.0*PI;
    if (angle < -PI) angle += 2.0*PI; 
    // crude debouncing       
    if (abs(angle-oldAngle) < .5 || abs(angle-oldAngle) > 6) {
//          Serial.print("Ang = ");
//          Serial.println(angle);
      // angle is now 0, going negative for CCW rotation
      range = bMax - bMin;
      // only 2.35 radians either side of center (~270 degrees total)
      if (angle >  2.35) angle = 2.35;
      if (angle < -2.35) angle = -2.35;        
      // log func, about 0+ to 1+, depends on angle
      twist = ( pow(2, angle) / 4.9) ;
      bright = (bMin + (twist * range) - (range/20));
 //       bright = map(( pow(2, angle) / 4.9),0,1,bMin*.95,bMax*1.05);
      if (bright < bMin) bright = bMin;
      if (bright > bMax) bright = bMax;
      if (bright != oldbright) {
#if DEBUG            
        Serial.print("bright=");
        Serial.println(bright);
#endif            
        analogWrite(DPIN_DRV_EN, bright);
        oldbright = bright;
      } // if oldbright
    }  // if old Angle
    oldAngle = angle;
  } // while switch not pressed
  accelOnOff(false);
/*      Serial.print("Ang = ");
  Serial.print(angle);
  Serial.print("\tTwist = ");
  Serial.print(twist);
  Serial.print("\tBright = ");
  Serial.println(bright);
*/
  blinkoff(6);
  return(bright);
}

void setCleanup(byte ee, byte br) {
  analogWrite(DPIN_DRV_EN, br);  
  EEPROM.write(ee,br);
  ee_write_cksum();
  time=millis();
  btnTime=time;
  btnDown = digitalRead(DPIN_RLED_SW);
  btnDownBefore = btnDown;
}

void setStartmode(byte sm) {
  startMode = sm;
  newMode = MODE_OFF;
  // only remember mode if changed to save wear on EEPROM
  byte ee_sm = EEPROM.read(EE_STARTMODE);
  if (ee_sm != startMode) {
    EEPROM.write(EE_STARTMODE,startMode);
    ee_write_cksum();        
  }
  //flash goodbye
  blinkoff(3);
}

