/**************************************************************************
*
*
**************************************************************************/
#define REMOTE_WIND_CLIENT_VERSION "v0.6"
//#define NO_GSM // run without communicating with GSM, good for debugging

#include <SoftwareSerial_by_YO.h>
#include <Time.h>
#include <RestClient.h>
#include "includes.h"
#include "version.h"
#include <avr/wdt.h>

#define arduinoLED 13
#define ledOn 1
#define ledOff 0

// the normal remote wind client is built on a Seeeduino V3.0 and the Seeedstudio GPRS shield
// http://www.seeedstudio.com/wiki/GPRS_Shield_V1.0
// http://www.seeedstudio.com/wiki/GPRS_Shield_V2.0
// http://www.seeedstudio.com/wiki/GPRS_Shield_V3.0
// These boards use the following pins
// 2 and 3 for hardware based serial communication
// 7 and 8 for software serial communication if jumpers in that position
// 9 for power on/off

// define rx and tx for debug info
// use pin 7 to receive from sim900 and pin 8 to send, thus interrupts on port B will ba available to detect wind speed 
// from the wind speed sensor on pin 10
// pin 7 is on port D so we need to use PCINT2
#define USE_PCINT2
#define rxPin 5
#define txPin 6
// define baudrates
#define GPRS_BAUD 19200
#define DEBUG_BAUD 19200

int stationId = 0;

#define WIND_SPEED_PIN 10 // digital pin 10
#define WIND_DIR_PIN 0 // analog 0
#define TURN_ON_PIN 9

// normally samples min and max every 15 seconds, for debugging 5 seconds is good
#define SAMPLE_PERIOD 30L  // period in seconds for which min and max samples are taken
#define MINUTE (2L*SAMPLE_PERIOD)
#define PERIOD (2L*MINUTE)      // period between samples sent to server, 5 minutes
#define ONCE_PER_HOUR (60L*MINUTE)
#define ONCE_PER_DAY (24L*ONCE_PER_HOUR)

// the perimeter is about 44 cm, we get two pulses per rotation, windPulses * 22 gives us distance in cm rotated
// we want to be able to measure up to 30-40m/s. 40 m/s will rotate the sensor 40/0.44 windPulses, we will get the double amount of pulses
// thats about 182 pulses per second, and we need to store 182*PERIOD in windPulses = 54 545 pulses, a byte is way to small
// unsigned int max is 65535 which is during 300s is about 48 m/s. probably the hardware wont sustain this:)

/******************************************************************************
 * Setup
 ******************************************************************************/
#define DEBUG_ACTIVE true
SoftwareSerial_by_YO softserial(rxPin,txPin); // rxpin, txpin
#define debug(...) do { \
  if (DEBUG_ACTIVE) { softserial.print(__VA_ARGS__); } \
} while (false)

#define debugln(...) do { \
  if (DEBUG_ACTIVE) { softserial.println(__VA_ARGS__); } \
} while (false)

char i,j;
void setup(){
 
#ifdef DEBUG_ACTIVE
  softserial.begin(DEBUG_BAUD);
#endif

#ifdef NO_GSM
  stationRegistered = true;
#endif
 
  debugln(F("RemoteWindClient started"));
  debugln(F("Build: " __DATE__ " " __TIME__));
  debug(  F("Arduino IDE version: "));debugln( ARDUINO, DEC);
  debug(  F("GSM lib version: "));debugln(GSMGPRS_SHIELD_VERSION);
  debug(  F("REST lib version: "));debugln(ARDUINO_REST_VERSION);
  debug(  F("Time lib version: "));debugln(TIME_VERSION);
  debug(  F("Version: "));debugln(F(VERSION));
  debug(  F("Hardware serial RX buffer size: "));debugln(SERIAL_RX_BUFFER_SIZE);
  
  // illuminate Arduino LED
  pinMode(arduinoLED, OUTPUT);
  digitalWrite(arduinoLED, ledOn);
  
  initConnectionManager();  
  initREST();
  connectMobileNetwork(); // activate the GSM modem so time can be synced in next step
  initTimekeeper();
  
  while(stationId==0) {
    setConnectionState(CONNECTED_TO_SERVER);
    if(CONNECTED_TO_SERVER == connectionState) {
      // ask server for station id using our imei
      char* resp = gsm.getIMEI();
      stationId = getStationId(resp);
    } 
    if( (CONNECTED_TO_SERVER != connectionState) 
      || (stationId == 0) ) {
      debugln(F("Failed to register station, re-try in 1 minute."));  
      // failed somehow to connect
      setConnectionState(NOT_CONNECTED);
      watchdogSafeDelay((1*60*1000L)); // wait 1 minute and try again
    }
  }

  setConnectionState(NOT_CONNECTED);
  
  debug(F("Registered as station with id "));debugln(stationId);
  
  debugln(F("Report current firmware"));
  bool firmwareReported = false;
  int tries = 0;
  while(!firmwareReported && (tries<3)) {
    setConnectionState(CONNECTED_TO_SERVER);
    if(CONNECTED_TO_SERVER == connectionState) {
      if(reportFirmware(stationId)) {
        firmwareReported = true;
      } else {
        tries++;
        // failed somehow to connect 
        debugln(F("Failed to report firmware, try once more"));    
      }
      setConnectionState(NOT_CONNECTED);
    }
  }
  
  // communication is up and we have registered station
  initWindSensor();
}

#define DELAY_INTERVAL 500
void watchdogSafeDelay(uint32_t delayTimeInMs) {
  wdt_reset();
  while(delayTimeInMs>DELAY_INTERVAL) {
    delay(DELAY_INTERVAL);
    delayTimeInMs -= DELAY_INTERVAL;
    wdt_reset();
  }
  delay(delayTimeInMs);
}

void watchdogSetup(void) {
  wdt_enable(WDTO_2S);
  debugln(F("- Watchdog initialized with 2000 ms interval."));
}


/******************************************************************************
 * measure values and report if enough time has passed
 ******************************************************************************/
long directionVectorX = 0;
long directionVectorY = 0;
short directionAvg;
unsigned long pulses;
unsigned long avgPulses;
unsigned long totalPulses = 0; // total amount of windPulses during PERIOD
unsigned long maxPulses = 0; // max windPulses during SAMPLE_PERIOD
unsigned long minPulses = 65535; // minimum amount of windPulses during SAMPLE_PERIOD

unsigned long timePassed = 0; // time in ms passed variable used to measure time for PERIOD and SAMPLE_PERIOD
unsigned long timePassedSinceLastSample = 0; // amount of time in ms since we last took a sample on values
unsigned long timePassedThisPeriod = 0; // the time in ms of this period (measured)

byte samplePeriodsPassed = 0; // number of SAMPLE_PERIODS passed so far during thie PERIOD
void measure() {
  // if a sample period has passed, calculate this periods min max
  if(timePassed >= SAMPLE_PERIOD*1000L) { // timePassed is calculated in loop()
    
    samplePeriodsPassed += timePassed/(SAMPLE_PERIOD*1000L); // keep track of how many periods the total includes
    timePassedSinceLastSample = timePassed;
    timePassed = 0;
     
    timePassedThisPeriod += timePassedSinceLastSample; 
       
    pulses = getAndResetWindSpeed()*100L;  // scale up the pulse variable 100 times to have two decimals in the averages
    
    // calculate approx windPulses / s
    // we measure time in millis seconds and the minimum we can stora is 1 rotation / s and we update this twize every second so we
    // need to use the milliseconds timing thus multiply the windPulses with 1000 since we cannot handle fractions
    // so if pulses == 1, then that would give us 2 if the time is 500 ms
    avgPulses = pulses * 1000L;
    debug(F("pulses = "));
    debug(pulses);
    debug(F(", 1000*pulses = "));
    debug(avgPulses);   
    avgPulses = avgPulses/timePassedSinceLastSample;
    debug(F(", avg pulses = "));
    debugln(avgPulses);
    
    // if we have more or less windPulses than previous store new values
    if(maxPulses < avgPulses) {
      maxPulses = avgPulses;
    }
    if(minPulses > avgPulses) {
      minPulses = avgPulses;
    }
    totalPulses += pulses;
    debug(F("=> max pulses = "));
    debug(maxPulses);
    debug(F(", min pulses = "));
    debugln(minPulses);
   
    // Wind direction
    
    // Sum X and Y vectors (then use atan2 to calculate the average angle).
    // By multiplying the vector with pulses a stronger wind will contribute more to the calculated average
    // Only check direction sensor if we have wind.
    if (pulses > 0) {
      directionVectorY += (long)(sin(getWindDirection() * 3.14159 / 180) * pulses);
      directionVectorX += (long)(cos(getWindDirection() * 3.14159 / 180) * pulses);
    }  
  
    if (directionVectorX == 0 && directionVectorY == 0) { // if the direction sensor has not been read
      directionAvg = NULL;  // There is no wind, thus no wind direction
    }else{
      // atan2 gives an angle in the interval -pi --- pi
      directionAvg = atan2(directionVectorY, directionVectorX) * 180 / 3.14159; // Radians to degrees
      if (directionAvg < 0) directionAvg += 360;  // Let angle always be positive
    }
    
    debug(F("avg direction: "));
    debugln(directionAvg);
  }
}

unsigned int periodsPassed = 0; // keep counting number of periods to be able to do things once per hour or day
unsigned long startConnectionTime;
unsigned long lastReportTime;
char totalLength;
void report() {
  debug(F("Free RAM: "));debug(freeRam());debugln(F(" bytes."));
  periodsPassed++; // used to do things once an hour or day
  // period has passed, report results
    
  startConnectionTime = millis();

  // A wind speed of 1.492 MPH (2.4 km/h) causes the switch to close once per second. 1 pulse per second should be 0.6667 m/s
  // the perimeter is about 44 cm, we get two pulses per rotation => one pulse per second means half that distance, 22 cm which is not
  // 66.7 cm!
    
  // Measured radii = 7 cm, perimeter is 2 * pi * radii = 43.982288 cm
  // totalPulses are measured during PERIOD(25 seconds or 300 seconds(5 minutes))

  // totalPulses/PERIOD is pulses per second and if we multiply with 67 cm/s we get current speed in cm/s
  debug(F("tot pulses = "));
  debugln(totalPulses);
    
  // removed the perimeter scaling, lets have the calibration at the server instead!
  totalPulses = totalPulses/(samplePeriodsPassed*SAMPLE_PERIOD);
  samplePeriodsPassed = 0;

  setConnectionState(CONNECTED_TO_SERVER);
  int tries = 1;
  while(CONNECTED_TO_SERVER!=connectionState && tries<5) {
    setConnectionState(CONNECTED_TO_SERVER);
    tries++;
  }
  
  if(CONNECTED_TO_SERVER==connectionState) {
    reportObservation(stationId, directionAvg, totalPulses, minPulses, maxPulses);
    
    if(periodsPassed%(ONCE_PER_HOUR/PERIOD) == 0) {
      // anything we want to do once each hour
      // nothing at the moment
    }
        
    if(periodsPassed%(ONCE_PER_DAY/PERIOD) == 0) {
      // anything we want to do once each day
      periodsPassed = 0; // reset the counter
    }
      
    setConnectionState(NOT_CONNECTED);
    
    lastReportTime = millis() - startConnectionTime;
  }

  totalPulses = 0;
  maxPulses = 0;
  minPulses = 65535;
  samplePeriodsPassed = 0;
  timePassedThisPeriod = 0;
  directionVectorX = directionVectorY = 0;
  debug(F("Free RAM: "));debug(freeRam());debugln(F(" bytes."));
}

int freeRam() {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

/******************************************************************************
 * Loop
 ******************************************************************************/
unsigned long startTime;
unsigned int sleepTime = 500;
void loop() {
  startTime = millis();
  watchdogSafeDelay(sleepTime);
  measure();
  if(timePassedThisPeriod  >= PERIOD*1000L) {
    report();
  }
  timePassed += millis() - startTime;
  sleepTime = 500 - timePassed%500;
}
