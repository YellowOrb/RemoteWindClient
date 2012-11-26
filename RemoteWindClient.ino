/**************************************************************************
*
*
**************************************************************************/
#define REMOTE_WIND_CLIENT_VERSION "v0.6"
//#define NO_GSM // run without communicating with GSM, good for debugging

#include <SoftwareSerial.h>
#include <sim900REST.h>
#include <PinChangeInt.h>

//#define DEBUG_INFO

//#if defined(DEBUG_INFO)
#define printInfo(...) debugSerial.print(__VA_ARGS__)
#define printlnInfo(...) debugSerial.println(__VA_ARGS__); debugSerial.flush()
//#else
//#define debug(INFO,...)
//#define debugln(INFO,...)
//#endif

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
#define GPRS_BAUD 2400
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

char* idStr = "xxxx"; // string used to store this stations is, registerStation fills in this value and is used when posting data
char* imeiStr = "AABBBBBBCCCCCCEE";

yamlDataT stationData[] = { 
  { "id", idStr, strlen(idStr) }, 
  { "hw_id", imeiStr, strlen(imeiStr) },
};

#define MEASURE_STR_SIZE 4
//max 4 chars to fit dir-values which can go up to 3600
//max 4 chars to fit windspeed 9999 cm/s = 99.99 m/s

//s    "speed"
//d    "direction"
//i "station_id"
//max    "max_wind_speed"
//min    "min_wind_speed"
//t    "temperature"
// "m[s]=xxxx&m[d]=xxxx&m[i]=xxxx&m[max]=xxxx&m[min]=xxxx&m[t]=xxxx"
char dataString[5+MEASURE_STR_SIZE+6+MEASURE_STR_SIZE+6+MEASURE_STR_SIZE+8+MEASURE_STR_SIZE+8+MEASURE_STR_SIZE+6+MEASURE_STR_SIZE]; //63 chars

// the perimeter is about 44 cm, we get two pulses per rotation, windPulses * 22 gives us distance in cm rotated
// we want to be able to measure up to 30-40m/s. 40 m/s will rotate the sensor 40/0.44 windPulses, we will get the double amount of pulses
// thats about 182 pulses per second, and we need to store 182*PERIOD in windPulses = 54 545 pulses, a byte is way to small
// unsigned int max is 65535 which is during 300s is about 48 m/s. probably the hardware wont sustain this:)

char startError = 0; // state variable to store any errors occuring during setup
#define START_ERROR_CANNOT_TURN_ON_GSM 1
#define START_ERROR_CANNOT_INIT_GSM 2
#define START_ERROR_CANNOT_READ_IMEI 3
#define START_ERROR_CANNOT_GET_GPRS 4
#define START_ERROR_CANNOT_REGISTER 5

/******************************************************************************
 * Check that we have GPRS
 * @return bool 
 ******************************************************************************/
bool checkGPRSAndSetupContext(){
#ifdef NO_GSM  
  return true;
#endif
  int error;
  char i,j;
  // try 5 times to get GRPS up and running
  for(j=0;j<5;j++) {
    // turn on module
    // keep trying five times to succeed since we cannot continute if we do not do this
    for(i=0;i<5;i++) {
      if(!gprsModule.turnOnModule()){
        // failed
        delay(2000);
        printlnInfo("+");
      } else {
        // interrupt for-loop since we successfully turned on the GSM module
        break;
      }
    }
    if(5==i) {
      // if we didnt break out turnOff and restart
      if(!gprsModule.turnOffModule()) {
        printlnError("Failed off");
      }
      delay(1000);
      continue;
    }
  
    printlnInfo("on");
    
    for(i=0;i<10;i++) {
      // wait until we have GPRS available
      if(GPRS_AVAILABLE_OK!=(error=gprsModule.isGPRSAvailable())) {
        printInfo("No ");
        printInfo("GPRS");
        printInfo(": ");
        printlnInfo(error, DEC);
        if(2==error) {
          printInfo("Signal");
          printInfo(": ");
          printlnInfo(gprsModule.getSignalStrength(), DEC);
        }
        delay(2000);
      } else {
        // interrupt for-loop since we got GPRS up
        break;
      }
    }
    if(10==i) {
      // if we didnt break out turnOff and restart
      if(!gprsModule.turnOffModule()) {
        printlnError("Failed off");
      }
      delay(1000);
      continue;
    }
  
    printlnInfo("GPRS");
  
    for(i=0;i<3;i++) {
      if(GPRS_CONTEXT_OK!=(error=gprsModule.setupGPRSContext(apn,NULL,NULL))) {
        printInfo("No context");
        printInfo(": ");
        printlnInfo(error, DEC);
        delay(500);
      } else {
        // interrupt for-loop since we got a GPRS context
        break;
      }
    }
    if(3==i) {
      // if we didnt break out turnOff and return false
      if(!gprsModule.turnOffModule()) {
        printlnError("Failed off");
      }
      delay(1000);
      continue;
    }
    // if we reached this point all went well, break out from the for
    break;
  }
  
  // we could not get GPRS up and running after five tries, give up
  if(5==j) {
    // if we didnt break out turnOff and return false
    if(!gprsModule.turnOffModule()) {
      printlnError("Failed off");
    }
    return false;
  }
  return true;
}

void postPrePaidBalance() {
  // read the balance from out prepaid card and report it back
  unsigned int tmpValue = gprsModule.getPrePaidBalance("*120#", "Saldo", "kr.");
  printlnInfo(tmpValue, DEC);
  if(0xffff!=tmpValue) {
    strcpy(dataString, "s[b]=");
    itoa(tmpValue, &dataString[strlen(dataString)], 10); // will null terminate the string in dataString
     
    printInfo("Post");
    printInfo(": ");
    printInfo(dataString);
    
    char *resourcePath = &dataString[strlen(dataString)+1];
    strcpy(resourcePath,"/s/");
    strcat(resourcePath, idStr);

    httpResponse = gprsModule.put(serverStr, portNr, resourcePath, dataString);
    if(200 == httpResponse) {
      printlnInfo(" OK");
    } else {
      printInfo(" err");
      printInfo(": ");
      printlnInfo(httpResponse,DEC);
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
  strcpy(dataString,"/stations/");
  strcat(dataString,"find/");
  strcat(dataString, imeiStr);
  gprsModule.yamlResponse = gprsModule.getWithYAMLResponse(serverStr, portNr, dataString, stationData, 2);
  if(200 == gprsModule.yamlResponse.resultCode) {
    // we found our imei registered
    printInfo("Station");
    printInfo(": ");
    printlnInfo(idStr);
  } else if(404 == gprsModule.yamlResponse.resultCode) {
    // station not found
    printlnInfo("Not found");
    // our imei was not in the server, register as a new station
    char *postData = dataString + (strlen(dataString)+1)*sizeof(char);
    strcpy(postData, "station[hw_id]=");
    strcat(postData, imeiStr);
    
    printInfo("Post");
    printInfo(": ");
    printlnInfo(postData);
    
    // register us
    httpResponse = gprsModule.post(serverStr, portNr, "/stations/", postData);
    if(200 == httpResponse) {
      printlnInfo(" OK");
      // check we got registered and get our stationID
      gprsModule.yamlResponse = gprsModule.getWithYAMLResponse(serverStr, portNr, dataString, stationData, 2);
      if(200 == gprsModule.yamlResponse.resultCode) {
        // we found our imei registered
        printError("Station");
        printError(": ");
        printlnError(idStr);
      } else if(404 == gprsModule.yamlResponse.resultCode) {
        printlnError("Not found");
        return false;
      } else {
        printlnError("Failed");
        return false;
      }
    } else {
      printInfo(" err");
      printInfo(": ");
      printlnInfo(httpResponse, DEC);
      return false;
    }
  } else {
    printInfo(" err");
    printInfo(": ");
    printlnInfo(gprsModule.yamlResponse.resultCode, DEC);
    return false;
  }
  // idStr now has the correct id of this station
  return true;
}

/******************************************************************************
 * Setup
 ******************************************************************************/
static bool stationRegistered = false; // set by calling registerStation()
char i,j;
void setup(){
    // setup serial ports
  debugSerial.begin(DEBUG_BAUD);               // used for debugging
#ifndef NO_GSM
  gprsSerial.begin(GPRS_BAUD);
#endif
  printInfo("RemoteWindClient ");
  printlnInfo(REMOTE_WIND_CLIENT_VERSION);
  printInfo("SIM900Module ");
  printlnInfo(SIM900_MODULE_VERSION);
  
  // illuminate Arduino LED
  pinMode(arduinoLED, OUTPUT);
  digitalWrite(arduinoLED, ledOn);
  
  do {
    // inidicate startError if we have one, i.e. restart of setup
    if(0!=startError){
      printInfo("Start");
      printInfo(" err");
      printInfo(": ");
      printlnInfo(startError);
      // repeat error code blinking for 15 minutes, ie. 30 half minutes
      for(j=0;j<30;j++) {
        for(i=0;i<10;i++) {
          // blink during 10 seconds
          if(startError>0) {
            digitalWrite(arduinoLED, 1);
          }
          delay(500);
          if(startError>0) {
            digitalWrite(arduinoLED, 0);
            startError--;
          }
          delay(500);
        } // end for
        delay(20000); // wait another 20 seconds
      } // end for
    } // make a new try to register and setup
    
#ifndef NO_GSM
    // turn on module
    // keep trying five times to succeed since we cannot continute if we do not do this
    for(i=0;i<5;i++) {
      if(!gprsModule.turnOnModule()){
        // failed
        delay(2000);
        printlnInfo("+");
      } else {
        // interrupt for-loop since we successfully turned on the GSM module
        break;
      }
    }
    if(5==i) {
      // if we didnt break out try to turn off and restart setup
      if(!gprsModule.turnOffModule()) {
        printlnError("Failed off");
      }
      startError = START_ERROR_CANNOT_TURN_ON_GSM;
      continue;
    }

    // configure and initialize GSM module communication
    for(i=0;i<5;i++) {
      if(!gprsModule.init()) {
        // failed
        delay(2000);
        printlnInfo("-");
      } else {
        // interrupt for-loop since we successfully initialized GSM
        break;
      }
    }
    if(5==i) {
      // if we didnt break out try to turn off and restart setup
      if(!gprsModule.turnOffModule()) {
        printlnError("Failed off");
      }
      startError = START_ERROR_CANNOT_INIT_GSM;
      continue;
    }
    
    // read our IMEI
    const char* result = gprsModule.getIMEI();
    if(strlen(result)>0) {
      strcpy(imeiStr,result);
    } else {
      // we could not read the IMEI, try to turn off and restart setup
      if(!gprsModule.turnOffModule()) {
        printlnError("Failed off");
      }
      startError = START_ERROR_CANNOT_READ_IMEI;
      continue;
    }
#endif
    printInfo("IMEI");
    printInfo(": ");
    printlnInfo(imeiStr);
    if(!gprsModule.turnOffModule()) {
      printlnError("Failed off");
    }  
    if(checkGPRSAndSetupContext()) {
      printlnError("GPRS up");
  
      // try to register us
      for(i=0;i<5;i++) {
        if(!(stationRegistered = registerStation())) {
          // failed
          printlnInfo(".");
          delay(10000); // wait 10 seconds before next attempt
        } else {
          // interrupt for-loop since we successfully registered
          break;
        }
      }
      if(5==i) {
        // if we didnt break out try to turn off and restart setup
        if(!gprsModule.turnOffModule()) {
          printlnError("Failed off");
        }
        startError = START_ERROR_CANNOT_REGISTER;
        continue;
      }
      // read service provider and post pre paid balance
      postPrePaidBalance();
  
      gprsModule.destroyGPRSContext();
      printlnError("GPRS down");
      
      if(!gprsModule.turnOffModule()) {
        printlnError("Failed off");
      }
    } else {
      startError = START_ERROR_CANNOT_GET_GPRS;
      continue;
    }
  } while(!stationRegistered);
  
  // communication is up and we have registered station
  setupSensors();
  
  printInfo("Station");
  printInfo(": ");
  printlnInfo(idStr);
}

/******************************************************************************
 * measure values and report if enough time has passed
 ******************************************************************************/
long unsigned int directionSum = 0; //max is 1890000
unsigned int directionSamples = 0;
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
  // measure wind direction and add it to the directionSum
  directionSum += getWindDirection();
  directionSamples++;
  
  printError(timePassed,DEC);
  printError(", ");
  
  // if a sample period has passed, calculate this periods min max
  if(timePassed >= SAMPLE_PERIOD*1000L) { // timePassed is calculated in loop()
    printlnError(NULL);
    
    samplePeriodsPassed += timePassed/(SAMPLE_PERIOD*1000L); // keep track of how many periods the total includes
    timePassedSinceLastSample = timePassed;
    timePassed = 0;
     
    timePassedThisPeriod += timePassedSinceLastSample;
 
    printInfo( timePassedThisPeriod);
    printInfo( "(");
    printInfo( timePassedSinceLastSample);
    printInfo( "), ");   
    
    pulses = getAndResetWindSpeed()*100L; // scale up the pulse variable 100 times
    
    // calculate approx windPulses / s
    // we measure time in millis seconds and the minimum we can stora is 1 rotation / s and we update this twize every second so we
    // need to use the milliseconds timing thus multiply the windPulses with 1000 since we cannot handle fractions
    // so if pulses == 1, then that would give us 2 if the time is 500 ms
    avgPulses = pulses * 1000L;
    printlnError(NULL);
    printError(" pulses = ");
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
  }
}

unsigned int periodsPassed = 0; // keep counting number of periods to be able to do things once per hour or day
unsigned long startConnectionTime;
unsigned long lastReportTime;
char totalLength;
void report() {  
  periodsPassed++; // used to do things once an hour or day
  // period has passed, report results
    
  startConnectionTime = millis();

  // A wind speed of 1.492 MPH (2.4 km/h) causes the switch to close once per second. 1 pulse per second should be 0.6667 m/s
  // the perimeter is about 44 cm, we get two pulses per rotation => one pulse per second means half that distance, 22 cm which is not
  // 66.7 cm!
    
  // Measured radii = 7 cm, perimeter is 2 * pi * radii = 43.982288 cm
  // totalPulses are measured during PERIOD(25 seconds or 300 seconds(5 minutes))

  // totalPulses/PERIOD is pulses per second and if we multiply with 67 cm/s we get current speed in cm/s
  printError("tot pulses = ");
  printlnError(totalPulses);
    
  printlnInfo();
  
  totalPulses = totalPulses/(samplePeriodsPassed*SAMPLE_PERIOD);
  samplePeriodsPassed = 0;
  printInfo("w:");
  printInfo((totalPulses*67L)/100L, DEC);
  printInfo("(");
  printInfo((maxPulses*67L)/100L, DEC);
  printInfo("/");
  printInfo((minPulses*67L)/100L, DEC);
  printlnInfo(")");
    
  // average over a number of running samples
  printInfo("d:");
  directionSum = directionSum/directionSamples; // divide with number of times we have read the direction to get an average
  printlnInfo(directionSum, DEC);
    
  if(stationRegistered && checkGPRSAndSetupContext() ) {
    // build up post data
    // "m[s]=xxxx&m[d]=xxxx&m[i]=xxxx&m[max]=xxxx&m[min]=xxxx&m[t]=xxxx"
    // char dataString[5+4+6+4+6+4+8+4+8+4+6+4]; //63 chars

    // windPulses
    strcpy(dataString, "m[s]=");
    itoa((totalPulses*67L)/100L, &dataString[strlen(dataString)], 10);
      
    strcat(dataString, "&m[d]=");
    itoa(directionSum, &dataString[strlen(dataString)], 10);
           
    strcat(dataString, "&m[i]=");
    strcat(dataString, idStr);
      
    strcat(dataString, "&m[max]=");
    itoa((maxPulses*67L)/100L, &dataString[strlen(dataString)], 10);
    
    strcat(dataString, "&m[min]=");
    itoa((minPulses*67L)/100L, &dataString[strlen(dataString)], 10);

#ifdef MEASTURE_TEMP
    strcat(dataString, "&m[t]=");
#ifdef NO_GSM
    itoa(20, &dataString[strlen(dataString)], 10);
#else
    itoa(gprsModule.getTemperatur(), &dataString[strlen(dataString)], 10);
#endif
#endif     
    printInfo( "Post");
    printInfo(": "); 
    printInfo(dataString);

#ifndef NO_GSM
    httpResponse = gprsModule.post(serverStr, portNr, "/measures/", dataString);
    if(200 == httpResponse) {
      printlnInfo(" OK");
    } else {
      printInfo(" err");
      printInfo(": ");
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
    lastReportTime = millis() - startConnectionTime;
  }
#endif
  directionSum = 0;
  directionSamples = 0;
  totalPulses = 0;
  maxPulses = 0;
  minPulses = 65535;
  samplePeriodsPassed = 0;
  timePassedThisPeriod = 0;
}

/******************************************************************************
 * Loop
 ******************************************************************************/
unsigned long startTime;
unsigned int sleepTime = 500;
void loop(){
  startTime = millis();
  delay(sleepTime);
//  printInfo("Time ");
//  printInfo(timePassed);
//  printInfo(" Sleept ");
//  printlnInfo(sleepTime);
  measure();
  if(timePassedThisPeriod  >= PERIOD*1000L) {
    report();
  }
  timePassed += millis() - startTime;
  sleepTime = 500 - timePassed%500;
}




