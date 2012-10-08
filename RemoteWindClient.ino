//#define NO_GSM // run without communicating with GSM, good for debugging

#include <SoftwareSerial.h>
#include <sim900REST.h>
#include <PinChangeInt.h>
#include <MsTimer2.h>

#define DEBUG_INFO

#if defined(DEBUG_INFO)
#define printInfo(...) debugSerial.print(__VA_ARGS__)
#define printlnInfo(...) debugSerial.println(__VA_ARGS__); debugSerial.flush()
#else
//#define debug(INFO,...)
//#define debugln(INFO,...)
#endif

#ifdef DEBUG_ERROR
#define printError(...) debugSerial.print(__VA_ARGS__)
#define printlnError(...) debugSerial.println(__VA_ARGS__); debugSerial.flush()
#else
#define printError(...)
#define printlnError(...)
#endif

#define arduinoLED 13
#define ledOn 1
#define ledOff 0

#define rxPin 7
#define txPin 8
#define GPRS_BAUD 4800
#define DEBUG_BAUD 19200

HardwareSerial debugSerial = Serial;
SoftwareSerial gprsSerial(rxPin, txPin);
sim900REST gprsModule(gprsSerial, &Serial);

char* apn = "online.telia.se"; // APN for mobile subscription
char* serverStr = "blast.nu"; // server where to post data
int portNr = 80; // TCP/IP port for the webserver, usually 80
int16_t httpResponse;

#define WIND_SPEED_PIN 10 // digital pin 10
#define WIND_DIR_PIN 0 // analog 0
#define TURN_ON_PIN 9

// normally samples min and max every 15 seconds, for debugging 5 seconds is good
#define SAMPLE_PERIOD 30L  // period in seconds for which min and max samples are taken
#define MINUTE (2L*SAMPLE_PERIOD)
#define PERIOD (5L*MINUTE)      // period between samples sent to server, 5 minutes
#define ONCE_PER_HOUR (60L*MINUTE)
#define ONCE_PER_DAY (24L*ONCE_PER_HOUR)
unsigned long timePassed = 0; // time passed variable used to measure time for PERIOD and SAMPLE_PERIOD
unsigned long timePassedSinceLastSample = 0; // amount of time since we last took a sample on values
unsigned long timePassedThisPeriod = 0; // the time this period was(measured)
byte samplePeriodsPassed = 0; // number of SAMPLE_PERIODS passed so far during thie PERIOD
unsigned int periodsPassed = 0; // keep counting number of periods to be able to do things once per hour or day

#define MEASURE_STR_SIZE 4

#define STATION_RESOURCE_SIZE 10
char* stationResource = "/stations/";
char* idStr = "xxxx"; // string used to store this stations is, registerStation fills in this value and is used when posting data
char* imeiStr = "AABBBBBBCCCCCCEE";
#define IMEI_SIZE 17

yamlDataT stationData[] = { 
  { "id", idStr, strlen(idStr) }, 
  { "hw_id", imeiStr, strlen(imeiStr) },
};

const char* result;

//s    "speed"
//d    "direction"
//i "station_id"
//max    "max_wind_speed"
//min    "min_wind_speed"
//t    "temperature"
// "m[s]=xxxx&m[d]=xxxx&m[i]=xxxx&m[max]=xxxx&m[min]=xxxx&m[t]=xxxx"
char measureData[5+MEASURE_STR_SIZE+6+MEASURE_STR_SIZE+6+MEASURE_STR_SIZE+8+MEASURE_STR_SIZE+8+MEASURE_STR_SIZE+6+MEASURE_STR_SIZE]; //63 chars

// "measure[speed]=5.6&measure[direction]=183&measure[station_id]=183"
//char measureData[15+4+20+4+21+4];

char measureStr[MEASURE_STR_SIZE+1]; // short text string to store ascii versions of readings, +1 to fit trailing 0
//max 4 chars to fit dir-values which can go up to 3600
//max 4 chars to fit windspeed 9999 cm/s = 99.99 m/s
char postData[IMEI_SIZE+15]; // string for posting IMEI data
char resourcePath[STATION_RESOURCE_SIZE+5+IMEI_SIZE]; //

// the perimeter is about 44 cm, we get two pulses per rotation, windPulses * 22 gives us distance in cm rotated
// we want to be able to measure up to 30-40m/s. 40 m/s will rotate the sensor 40/0.44 windPulses, we will get the double amount of pulses
// thats about 182 pulses per second, and we need to store 182*PERIOD in windPulses = 54 545 pulses, a byte is way to small
// unsigned int max is 65535 which is during 300s is about 48 m/s. probably the hardware wont sustain this:)
volatile unsigned int windPulses=0;    // a counter to see how many times the pin has changed, defined volatile since set by an interrupt

unsigned long totalPulses = 0; // total amount of windPulses during PERIOD
unsigned long maxPulses = 0; // max windPulses during SAMPLE_PERIOD
unsigned long minPulses = 65535; // minimum amount of windPulses during SAMPLE_PERIOD

char led = 0; // state variable for flashing the Arduino led at each input pulse


/******************************************************************************
 * Check that we have GPRS
 * @return bool 
 ******************************************************************************/
bool checkGPRSAndSetupContext(){
#ifdef NO_GSM  
  return true;
#endif
  // wait until we have GPRS available
  while(!gprsModule.isGPRSAvailable()) {
    printlnError("No GPRS");
    delay(5000);
  }
  return gprsModule.setupGPRSContext(apn,NULL,NULL);
}


unsigned int tmpValue;
char length;
void postPrePaidBalance() {
  // read the balance from out prepaid card and report it back
  tmpValue = gprsModule.getPrePaidBalance("*120#", "Saldo", "kr.");
  printlnInfo(tmpValue, DEC);
  if(0xffff!=tmpValue) {
    itoa(tmpValue, measureStr, 10);
    length = strlen(measureStr);
    printlnInfo(length, DEC);
    if(IMEI_SIZE+15>length+5) {
      // postData will fit the info
      strcpy(postData, "s[b]=");
      strcpy(&postData[5], measureStr);
      postData[length+5] = '\0';
      
      printInfo("Post: ");
      printInfo(postData);
      
      strncpy(resourcePath,"/s/",3);
      length = strlen(idStr);
      strncpy(&resourcePath[3], idStr, length);
      resourcePath[3+length] = '\0'; // end the str

      httpResponse = gprsModule.put(serverStr, portNr, resourcePath, postData);
      if(200 == httpResponse) {
        printlnInfo(" OK");
      } else {
        printInfo(" err: ");
        printlnInfo(httpResponse,DEC);
      }
    }
  }
}

/******************************************************************************
 * check if we are registered and get station ID or register us and get our station ID
 * @return bool 
 ******************************************************************************/
bool registerStation(void) {
#ifdef NO_GSM
  stationRegistered = true;
  return true;
#endif

  // get station id for reporting measures
  strncpy(resourcePath,stationResource,STATION_RESOURCE_SIZE);
  strncpy(&resourcePath[STATION_RESOURCE_SIZE],"find/",5);
  strncpy(&resourcePath[STATION_RESOURCE_SIZE+5],imeiStr,IMEI_SIZE);
  gprsModule.yamlResponse = gprsModule.getWithYAMLResponse(serverStr, portNr, resourcePath, stationData, 2);
  if(200 == gprsModule.yamlResponse.resultCode) {
    // we found our imei registered
    printError("Station:");
    printlnError(idStr);
  } else if(404 == gprsModule.yamlResponse.resultCode) {
    // station not found
    printlnError("Not found");
    // our imei was not in the server, register as a new station
    strncpy(postData, "station[hw_id]=", 15);
    strncpy(&postData[15], imeiStr, IMEI_SIZE);
    printInfo("Post: ");
    printlnInfo(postData);
    
    // register us
    httpResponse = gprsModule.post(serverStr, portNr, stationResource, postData);
    if(200 == httpResponse) {
      printlnInfo(" OK");
      // check we got registered and get our stationID
      gprsModule.yamlResponse = gprsModule.getWithYAMLResponse(serverStr, portNr, resourcePath, stationData, 2);
      if(200 == gprsModule.yamlResponse.resultCode) {
        // we found our imei registered
        printError("Station:");
        printlnError(idStr);
      } else if(404 == gprsModule.yamlResponse.resultCode) {
        printlnError("Not found");
        return false;
      } else {
        printlnError("Failed");
        return false;
      }
    } else {
      printInfo(" err: ");
      printlnInfo(httpResponse, DEC);
      return false;
    }
  } 
  // idStr now has the correct id of this station
  return true;
}

/******************************************************************************
 * Setup
 ******************************************************************************/
 static bool stationRegistered = false; // set by calling registerStation()
void setup(){
  // setup serial ports
  debugSerial.begin(DEBUG_BAUD);               // used for debugging
#ifndef NO_GSM
  gprsSerial.begin(GPRS_BAUD);
#endif
  printlnInfo("Init");
  
  // illuminate Arduino LED
  pinMode(arduinoLED, OUTPUT);
  digitalWrite(arduinoLED, ledOn);

#ifndef NO_GSM
// configure and initialize GSM module communication
// keep on trying till we succeed since we cannot continute if we do not do this
// turn on module
  while(!gprsModule.turnOnModule()) {
    delay(2000);
    printlnInfo("+");
  }

  while(!gprsModule.init()) {
    delay(2000);
    printlnInfo("-");
  }

  // read our IMEI
  result = gprsModule.getIMEI();
  if(strlen(result)>0) {
    strncpy(imeiStr,result,IMEI_SIZE);
  }
#endif
  printInfo("IMEI: ");
  printlnInfo(imeiStr);
  
  //tmpValue = gprsModule.getPrePaidBalance("*120#", "Saldo", "kr.");
  //printlnInfo(tmpValue, DEC);
  if(checkGPRSAndSetupContext()) {
    printlnError("GPRS up");
  
    // try to register us
    while(!(stationRegistered = registerStation())) {
      printlnInfo(".");
      delay(30000); // wait 30 seconds before next attempt
    }
  
    // read service provider and post pre paid balance
    postPrePaidBalance();
  
    gprsModule.destroyGPRSContext();
    printlnError("GPRS down");
  }
  
  setupSensors();
  
  printInfo("Station reg: ");
  printlnInfo( idStr);
  
  if(!gprsModule.turnOffModule()) {
    printlnError("Failed off");
  }
}

/******************************************************************************
 * measure values and report if enough time has passed
 ******************************************************************************/
long unsigned int dir_avr = 0; //max is 1890000
unsigned long pulses;
unsigned long avgPulses;
char totalLength;
void measureAndReport() {
  // measure wind direction and add it to the dir_avr
  dir_avr += getWindDirection();
  
  printError(timePassed,DEC);
  printError(", ");
  
  // if a sample period has passed, calculate this periods min max
  if(timePassed >= SAMPLE_PERIOD*1000L) { // timePassed is calculated in loop()
    printlnError(NULL);
    timePassedSinceLastSample = timePassed;
    
    printInfo( timePassedSinceLastSample);
    printInfo( ", ");
    
    timePassedThisPeriod += timePassedSinceLastSample;
    
    pulses = windPulses*100L; // scale up the pulse variable 100 times
    windPulses = 0;
    
    // calculate approx windPulses / s
    // we measure time in millis seconds and the minimum we can stora is 1 rotation / s and we update this twize every second so we
    // need to use the milliseconds timing thus multiply the windPulses with 1000 since we cannot handle fractions
    // so if pulses == 1, then that would give us 2 if the time is 500 ms
    avgPulses = pulses * 1000L;
    printError(", pulses = ");
    printError(pulses);
    printError(", 1000*pulses = ");
    printError(avgPulses);   
    avgPulses = avgPulses/timePassedSinceLastSample;
    printError(", avg pulses = ");
    printError(avgPulses);
    
    // if we have more or less windPulses than previous store new values
    if(maxPulses < avgPulses) {
      maxPulses = avgPulses;
    }
    if(minPulses > avgPulses) {
      minPulses = avgPulses;
    }
    totalPulses += pulses;
    printError("=> max pulses = ");
    printError(maxPulses);
    printError(", min pulses = ");
    printlnError(minPulses);
    
    samplePeriodsPassed += timePassed/(SAMPLE_PERIOD*1000L);
    timePassed = timePassed%(SAMPLE_PERIOD*1000L);
    printError(", time = ");
    printError(timePassed);
    printError(" periods = ");
    printlnError(samplePeriodsPassed);
  }
  
  if(samplePeriodsPassed >= (PERIOD/SAMPLE_PERIOD)) {
    periodsPassed++;
    // period has passed, report results
    // A wind speed of 1.492 MPH (2.4 km/h) causes the switch to close once per second. 1 pulse per second should be 0.6667 m/s
    // the perimeter is about 44 cm, we get two pulses per rotation => one pulse per second means half that distance, 22 cm which is not
    // 66.7 cm!
    
    // Measured radii = 7 cm, perimeter is 2 * pi * radii = 43.982288 cm
    // totalPulses are measured during PERIOD(25 seconds or 300 seconds(5 minutes))

    // totalPulses/PERIOD is pulses per second and if we multiply with 67 cm/s we get current speed in cm/s
    printError("tot pulses = ");
    printlnError(totalPulses);
    
    printlnInfo();
    
    printInfo("w:");
    printInfo((totalPulses*67L)/PERIOD/100L, DEC);
    printInfo("(");
    printInfo((maxPulses*67L)/100L, DEC);
    printInfo("/");
    printInfo((minPulses*67L)/100L, DEC);
    printlnInfo(")");
    
    // average over a number of running samples
    printInfo("d:");
    dir_avr = dir_avr/(2*PERIOD); // divide with 2 times PERIOD since we get 2 samples per second and PERIOD is defined in seconds
    printlnInfo(dir_avr, DEC);
    
    while(!gprsModule.turnOnModule()) {
      delay(2000);
      printlnInfo("+");
    }

    if(stationRegistered && checkGPRSAndSetupContext() ) {
      // build up post data
      // "m[s]=xxxx&m[d]=xxxx&m[i]=xxxx&m[max]=xxxx&m[min]=xxxx&m[t]=xxxx"
      // char measureData[5+4+6+4+6+4+8+4+8+4+6+4]; //63 chars

      // windPulses
      strncpy(measureData, "m[s]=", 5);
      itoa((totalPulses*67L)/PERIOD/100L, measureStr, 10);
      length=strlen(measureStr);
      strncpy(&measureData[5], measureStr, length);
      totalLength = 5 + length;
      
      strncpy(&measureData[totalLength], "&m[d]=", 6);
      itoa(dir_avr, measureStr, 10);
      length = strlen(measureStr);
      strncpy(&measureData[totalLength+6], measureStr, length);
      totalLength += 6 + length;      
      
      strncpy(&measureData[totalLength], "&m[i]=", 6);
      length = strlen(idStr);
      strncpy(&measureData[totalLength+6], idStr, length);
      totalLength += 6 + length;
      
      strncpy(&measureData[totalLength], "&m[max]=", 8);
      itoa((maxPulses*67L)/100L, measureStr, 10);
      length = strlen(measureStr);
      strncpy(&measureData[totalLength+8], measureStr, length);
      totalLength += 8 + length;
    
      strncpy(&measureData[totalLength], "&m[min]=", 8);
      itoa((minPulses*67L)/100L, measureStr, 10);
      length = strlen(measureStr);
      strncpy(&measureData[totalLength+8], measureStr, length);
      totalLength += 8 + length;

#ifdef MEASTURE_TEMP
      strncpy(&measureData[totalLength], "&m[t]=", 6);
#ifdef NO_GSM
      itoa(20, measureStr, 10);
#else
      itoa(gprsModule.getTemperatur(), measureStr, 10);
#endif
      length = strlen(measureStr);
      strncpy(&measureData[totalLength+6], measureStr, length);
      totalLength += 6 + length;
#endif

      measureData[totalLength] = 0; // end the str
     
      printInfo( "Post: "); 
      printInfo(measureData);

#ifndef NO_GSM
      httpResponse = gprsModule.post(serverStr, portNr, "/measures/", measureData);
      if(200 == httpResponse) {
        printlnInfo(" OK");
      } else {
        printInfo(" err: ");
        printlnInfo(httpResponse,DEC);
      }
      
      if(periodsPassed%(ONCE_PER_HOUR/PERIOD) == 0) {
        // anything we want to do once each hour
        // nothing at the moment
      }
        
      if(periodsPassed%(ONCE_PER_DAY/PERIOD) == 0) {
        // anything we want to do once each day
        periodsPassed = 0; // reset the counter
        postPrePaidBalance();
      }
      
      gprsModule.destroyGPRSContext();
      printlnError("GPRS down");
      
      if(!gprsModule.turnOffModule()) {
        printlnError("Failed off");
      }
    }
#endif
    dir_avr = 0;
    totalPulses = 0;
    maxPulses = 0;
    minPulses = 65535;
    samplePeriodsPassed = 0;
    timePassedThisPeriod = 0;
  }
}

/******************************************************************************
 * Loop
 ******************************************************************************/
unsigned long startTime;
void loop(){
  startTime = millis();
  delay(500);
  measureAndReport();
  timePassed += millis() - startTime;
}



