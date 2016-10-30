SIM900GPRS gsm;
SIM900Client client(&gsm);
uint8_t tries;

//#define HOST "www.yelloworb.com"
//#define PORT 3000

#define HOST "www.blast.nu"
#define PORT 80

//#define HOST "remote-wind-staging.herokuapp.com"
//#define PORT 80

void initConnectionManager() {
  Serial.begin(GPRS_BAUD);
  //gsm.setDebugSerial(&softserial);
  gsm.turnOn();
  gsm.activateDateTime();
  debug(F("- Connection manager has initialized GSM modem. GSM module software version: "));debugln(gsm.getSoftwareVersion());
}

/**
 * Turn on GSM modem and connect to the mobile network
 */
bool connectMobileNetwork() {
  if(!gsm.turnOn()) {
    gsm.shutdown();
    if(!gsm.turnOn()) {
      return false;
    }
  }
  
  debugln(F("GSM is on"));
  
  tries=0;
  while(gsm.begin() != GSM_READY) {
    tries++;
    debugln(F("GSM not ready"));
    if(tries>5) {
      return false;
    }
    watchdogSafeDelay(1000);
  }
  connectionState = CONNECTED_TO_MOBILE_NETWORK;
  debugln(F("CONNECTED_TO_MOBILE NETWORK"));
  
  gsm.activateDateTime();

  return true;
}

/**
 * Disconnect from the mobile network and turn off GSM modem
 */
void disconnectMobileNetwork() {
  gsm.deactivateDateTime();
  gsm.shutdown();
  connectionState = NOT_CONNECTED;
  debugln(F("Disconnected from mobile network."));
}

/**
 * Activate internet connectivity, so user can connect to any server
 */
bool connectInternet() {
  tries=0;
  while(GPRS_READY != gsm.attachGPRS("online.telia.se", NULL, NULL)) {
    debug(F("GPRS not ready - "));
    int str = gsm.getSignalStrength();
    debug(F("signal strength: "));debugln(str);
    if(str != 0) {
      tries++;
    }
          
    if(str>0 && tries%4 == 0) { // each 4th failure, try to disconnect and reconnet the mobile network if we have str larger than 0
      disconnectMobileNetwork();
      connectMobileNetwork();
      if(tries>50) {
        return false;
      }  
    }
    watchdogSafeDelay(1000);
  }
  connectionState = CONNECTED_TO_INTERNET;
  debugln(F("CONNECTED_TO_INTERNET"));
  return true;
}

/**
 * Disable internet connectivity
 */
void disconnectInternet() {
  gsm.detachGPRS(); 
  connectionState = CONNECTED_TO_MOBILE_NETWORK;
  debugln(F("Disconnected from INTERNET but still connected to MOBILE NETWORK"));
}

/**
 * Connect to server, defined by HOST and PORT
 */
bool connectServer() {
  if(false == client.connect(HOST, PORT)) {
    debugln(F("Failed to connect"));
      return false;
  }
  connectionState = CONNECTED_TO_SERVER;
  debugln(F("CONNECTED_TO_SERVER"));
  return true;
}

/**
 * Disconnect from server
 */
void disconnectServer() {
  client.stop();
  connectionState = CONNECTED_TO_INTERNET;
  debugln(F("Disconnected from SERVER but still connected to INTERNET"));
}

void printConnectionState(connected_state_t state) {
  switch(state) {
    case CONNECTED_TO_SERVER:
      debug(F("CONNECTED_TO_SERVER"));
      break;
    case CONNECTED_TO_INTERNET:
      debug(F("CONNECTED_TO_INTERNET"));
      break;
    case CONNECTED_TO_MOBILE_NETWORK:
      debug(F("CONNECTED_TO_MOBILE_NETWORK"));
      break;
    case NOT_CONNECTED:
      debug(F("NOT_CONNECTED"));
      break;
  }
}

static connected_state_t previousConnectedState;
/**
 * Change the connection state from the current one to the desired. Check connected
 * afterwards to know if the desired state was successfully activated
 */
void setConnectionState(connected_state_t newState) {
  previousConnectedState = connectionState;
  debug(F("Switching connection state from "));printConnectionState(connectionState);debug(F(" to "));printConnectionState(newState);debugln(F("."));
  debug(F("setConnectionState start free RAM: "));debug(freeRam());debugln(F(" bytes."));
  switch(previousConnectedState) {
    case CONNECTED_TO_SERVER:
      switch(newState) {
        case CONNECTED_TO_SERVER:
          break;
        case CONNECTED_TO_INTERNET:
          disconnectServer();
          break;
        case CONNECTED_TO_MOBILE_NETWORK:
          disconnectServer();
          disconnectInternet();
          break;
        case NOT_CONNECTED:
          disconnectServer();
          disconnectInternet();
          disconnectMobileNetwork();
          break;
      }
      break;
    case CONNECTED_TO_INTERNET:
      switch(newState) {
        case CONNECTED_TO_SERVER:
          connectServer();
          break;
        case CONNECTED_TO_INTERNET:
          break;
        case CONNECTED_TO_MOBILE_NETWORK:
          disconnectInternet();
          break;
        case NOT_CONNECTED:
          disconnectInternet();
          disconnectMobileNetwork();
          break;
      }
      break;
    case CONNECTED_TO_MOBILE_NETWORK:
      switch(newState) {
        case CONNECTED_TO_SERVER:
          connectInternet();
          connectServer();
          break;
        case CONNECTED_TO_INTERNET:
          connectInternet();
          break;
        case CONNECTED_TO_MOBILE_NETWORK:
          break;
        case NOT_CONNECTED:
          disconnectMobileNetwork();
          break;
      }
      break;
    case NOT_CONNECTED:
      switch(newState) {
        case CONNECTED_TO_SERVER:
          connectMobileNetwork();
          wdt_reset();
          connectInternet();
          wdt_reset();
          connectServer();
          wdt_reset();
          break;
        case CONNECTED_TO_INTERNET:
          connectMobileNetwork();
          connectInternet();
          break;
        case CONNECTED_TO_MOBILE_NETWORK:
          connectMobileNetwork();
          break;
        case NOT_CONNECTED:
          break;
      }
      break;
  }
  debug(F("setConnectionState end free RAM: "));debug(freeRam());debugln(F(" bytes."));
}

/**
 * Restore connection state to the one that was active before last call to setConnectionState().
 * Only stores the previous one so consequtive calls to this function will not have any effect. 
 */
void restoreConnectionState() {
  setConnectionState(previousConnectedState);
}
