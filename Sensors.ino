// only want interrupt on port C
#define EI_NOTPORTC
#define EI_NOTPORTD
#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>

#define arduinoLED 13
char led = 0; // state variable for flashing the Arduino led at each input pulse

#define WIND_SPEED_PIN 10 // digital pin 10, port PB2
#define WIND_DIR_PIN 0 // analog 0

volatile unsigned int windPulses=0;    // a counter to see how many times the pin has changed, defined volatile since set by an interrupt

void initWindSensor() {
    // attach a simple counter on the interrupt of the rising edge on the wind speed sensor
    enableInterrupt(WIND_SPEED_PIN, measureWindSpeed, RISING);
    debugln(F("- wind sensor initialized."));
}

/******************************************************************************
 * measureWindSpeed is called on every edge on the wind rotation sensor, 
 * increase number of windPulses and toggle the Arduino LED
 ******************************************************************************/
void measureWindSpeed() {
  windPulses++;
  digitalWrite(arduinoLED, led);
  led = led ? 0:1;
}

unsigned int getAndResetWindSpeed(){
  unsigned int result = windPulses;
  windPulses = 0;
  return result;
}
/******************************************************************************
 * getWindDirectoin()
 ******************************************************************************/
unsigned int dir = 0;
int value;
unsigned int getWindDirection() {
  // read the analog input
  value = analogRead(WIND_DIR_PIN);
  
  //printError("Windsensor: ");
  //printlnError(value);
  
  
  // 158-171
 //  66-71
  // 97-108
 //  33-36
  // 43-52
 //  --
  // 260-287
 //  231-243
  // 490-510
 //  423-439
  // 553-587
 //  --
  // 586-630
 //  358-365
  // 376-402
 //  132-142
 
 //      126-199
 // 56-125  | 320-410    
 //       \ | /
 //   -55-- * -- 600-
 //       / | \
 // 200-319 | 531-599
 //      411-530 
 
 if((value>125) && (value<200)) {
    // north
    dir = 360;  
  } else if((value>319) && (value<411)) {
    // ne
    dir = 45;
  } else if(value>599) {
    // east
    dir = 90;
  } else if((value>530) && (value<600)) {
    // se
    dir = 135;
  } 
  else if((value>411) && (value<531)) {
    // south
    dir = 180;
  } 
  else if((value>199) && (value<320)) {
    // sw
    dir = 225;
  } 
  else if(value<56) {
    // west
    dir = 270;
  } 
  else if((value>55) && (value<126)) {
    // nw
    dir = 315;
  }
  
  //printError(" dir: ");
  //printlnError(dir);
  return dir;  
}





