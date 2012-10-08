/******************************************************************************
 * measureWindSpeed is called on every edge on the wind rotation sensor, 
 * increase number of windPulses and toggle the Arduino LED
 ******************************************************************************/
void measureWindSpeed() {
  windPulses++;
  digitalWrite(arduinoLED, led);
  led = led ? 0:1;
}

/******************************************************************************
 * getWindDirectoin()
 ******************************************************************************/
unsigned int dir = 0;
unsigned int previousDir = 0;
int value;
unsigned int getWindDirection() {
  // read the analog input
  value = analogRead(WIND_DIR_PIN);

  if((value>240) && (value<260)) {
    // north
    // check what we read previously, if on the west-side use 360 instead of 0 to build up an average which can go over 315.
    if(previousDir > 1800) {
      dir = 3600;
    } else {
      dir = 0;
    }
  } 
  else if((value>576) && (value<596)) {
    // ne
    dir = 450;
  } 
  else if((value>921) && (value<941)) {
    // east
    dir = 900;
  } 
  else if((value>855) && (value<875)) {
    // se
    dir = 1350;
  } 
  else if((value>740) && (value<760)) {
    // south
    dir = 1800;
  } 
  else if((value>406) && (value<426)) {
    // sw
    dir = 2250;
  } 
  else if((value>68) && (value<88)) {
    // west
    dir = 2700;
  } 
  else if((value>147) && (value<167)) {
    // nw
    dir = 3150;
  }
  previousDir = dir;
  return dir;
}

void setupSensors() {
    // attach a simple counter on the interrupt of the rising edge on the wind speed sensor
  PCintPort::attachInterrupt(WIND_SPEED_PIN, measureWindSpeed, RISING);
}




