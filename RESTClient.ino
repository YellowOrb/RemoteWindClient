#include <SIM900.h>
#include <RestClient.h>

#define RESPONSE_SIZE 200
char response[RESPONSE_SIZE];

bool syncTimeViaHeaders = false;
uint32_t noHTTPSyncsSinceLastGSMSync = 0;
uint32_t timeLastGSMSync = 0;


RestClient rest(&client, response, RESPONSE_SIZE);

char *bodyStr = "";

const char PROGMEM dateStr[] = "Date: DAY, DD MON YEAR HH:MM:SS GMT";
#define DATE_STR_CONTENT_START 5

const char PROGMEM contentTypeHeaderStr[] = "Content-Type: application/x-www-form-urlencoded";
#define CONTENT_TYPE_STR_CONTENT_START 14

void initREST() {
  debug(F("- REST client initialized."));
}

/**
 * Query gsm modem of current unix time, used by time-h to sync time
 */
time_t retrieveTime() {
  if(CONNECTED_TO_INTERNET==connectionState || CONNECTED_TO_MOBILE_NETWORK==connectionState) {
    time_t t = gsm.getUnixTime();
    if(0 != t) {
      debug(F("Time synced at "));printTime(t);debugln();
      disableTimeSyncViaHeaders();
      noHTTPSyncsSinceLastGSMSync = 0;
      timeLastGSMSync = 0;
    } else {
      debugln(F("GSM modem has not synced time."));
      enableTimeSyncViaHeaders();
    }
    return t;
  } else {
    debugln(F("Unable to retrieve time, either connected to a server or modem is off."));
    return 0;
  }
}

/**
 * Enable functionality so that the Date: header in HTTP responses will be used to sync time
 */
void enableTimeSyncViaHeaders() {
  syncTimeViaHeaders = true;
}

/**
 * Disable functionality so that the Date: header will not be used to sync time
 */
void disableTimeSyncViaHeaders() {
  syncTimeViaHeaders = false;
}

/**
 * Initialize a REST request, creating all needed headers for authentication etc
 * method parameter - GET, POST, PUT or DELETE
 * resourcePath parameter - path to the REST resource
 * body parameter - optional pointer to request body, set to "" if not used
 * hmacSign - set to true if a hmac signature should be generated for authenticated calls
 */
bool initializeRequest(HttpMethod_t method, char *resourcePath, const char *body, bool hmacSign = false) {
  int freeRAMAtStart = freeRam();
  char *header = rest.initialize(method, resourcePath);
  
  header = rest.addHeader(PSTR("Accept: application/json; version=1.0"));
  if(NULL == header) return false;
  
  header = rest.addHeader(contentTypeHeaderStr);
  if(NULL == header)  {
    return false;
  }
  
  client.beginWrite();
  
  if(freeRAMAtStart > freeRam()) {
    debug(F("initializeRequest cunsumes RAM: "));debug(freeRAMAtStart - freeRam());debugln(F(" bytes."));
  }
  return true;
}

const char monthShortNames_P[] PROGMEM = "ErrJanFebMarAprMayJunJulAugSepOctNovDec";

/**
 * Finalize the request. This is when the actual request happens.
 * connectionClosed parameter - pointer to boolen indicating if connection is still open or not, if open more requests can follow without connecting to server again
 */
int finishRequest(bool* connectionClosed) {
  int freeRAMAtStart = freeRam();
  char *dateHeader = NULL;
  size_t dateHeaderSize = 0;
  char *headers[2];
  size_t headerSizes[2];
  uint8_t noHeaders;
  bool syncTime = syncTimeViaHeaders; // make a copy of this state since it might change during the execution of some of the functions used here
  
  client.endWrite();
    
  if(syncTime) {
    dateHeaderSize = strlen_P(dateStr);
    dateHeader = (char *)malloc(dateHeaderSize);
    strlcpy_P(dateHeader, dateStr, 5); // copy in the "Date:"
  }
  
  char *connectionHeader = (char *)malloc(23);
  strcpy_P(connectionHeader, PSTR("Connection:"));
  
  if(syncTime) {
    headers[0] = dateHeader;
    headers[1] = connectionHeader;
    headerSizes[0] = dateHeaderSize+1;
    headerSizes[1] = 23;
    noHeaders = 2;
  } else {
    headers[0] = connectionHeader;
    headerSizes[0] = 23;
    noHeaders = 1;
  }
  
  int responseCode = rest.readResponse(response, RESPONSE_SIZE, headers, headerSizes, noHeaders);
  debug(F("Response: "));debugln(responseCode);

  // see if Connection: close or Connection: Close header was present
  *connectionClosed = (strstr_P(connectionHeader, PSTR("close")) != NULL) || (strstr_P(connectionHeader, PSTR("Close")) != NULL);
  
  if(syncTime && (responseCode==200  || responseCode==201)) {
    if(timeSet != timeStatus()) {
      // Example of dateHeader
      // Date: Tue, 14 Apr 2015 09:14:41 GMT
      // 01234567890123456789012345678901234

      int year;
      int month;
      int day;
      int hour;
      int min;
      int sec;
      char * tmpStrPtr = dateHeader;
      
      tmpStrPtr[13]=0;
      day = atoi(&tmpStrPtr[11]);
      tmpStrPtr += 14;

      tmpStrPtr[3]=0;
      char *shortMonthStr = (char *)malloc(strlen_P(monthShortNames_P));
      strcpy_P(shortMonthStr, monthShortNames_P);
      month = (strstr(shortMonthStr, tmpStrPtr)-shortMonthStr)/3;
      free(shortMonthStr);
      tmpStrPtr += 4;
  
      tmpStrPtr[4]=0;
      year = atoi(tmpStrPtr);
      tmpStrPtr += 5;
  
      tmpStrPtr[2]=0;
      hour = atoi(tmpStrPtr);
      tmpStrPtr += 3;
  
      tmpStrPtr[2]=0;
      min = atoi(tmpStrPtr);
      tmpStrPtr += 3;
  
      tmpStrPtr[2]=0;
      sec = atoi(tmpStrPtr);
      
      noHTTPSyncsSinceLastGSMSync++;
      
      // if this is the first HTTP time sync, store the time when we last got sync over GSM
      if(timeLastGSMSync == 0) {
        timeLastGSMSync =  getLastSyncTime();
      }
      setTime(hour, min, sec, day, month, year);
      // if timeLastGSMSync is still 0 means that we have never synced the clock via GSM so set it to now!
      if(timeLastGSMSync == 0) {
        timeLastGSMSync = now();
      }
      
      debug(F("Synced time via HTTP headers at "));printCurrentTime();debugln();
      debug(F("Synced "));debug(noHTTPSyncsSinceLastGSMSync);debug(F(" times. No GSM sync for "));debug(now()-timeLastGSMSync);debugln(F(" s."));
    }
  }
  
  if(syncTime) {
    free(dateHeader);
  }
  free(connectionHeader);
  
  if(freeRAMAtStart > freeRam()) {
    debug(F("finishRequest cunsumes RAM: "));debug(freeRAMAtStart - freeRam());debugln(F(" bytes."));
  }
  
  return responseCode;
}


/**
 * Read configuration from server.
 * Must be in connected state SERVER for this to work and is not checked for
 */
int getStationId(char * imeiStr) {
  debug(F("getStationId start free RAM: "));debug(freeRam());debugln(F(" bytes."));
  int stationId = 0;
  char *requestPath = (char *)malloc(16+16+1); 
  // '/stations/find/' + imeiStr and the ending 0. - 16 + 16 + 1 characters
  //  1234567890123456
  requestPath[0] = 0;
  strcat_P(requestPath, PSTR("/stations/find/"));
  strcat(requestPath, imeiStr);
  
  debug(F("Register station "));debugln(requestPath);

  if(initializeRequest(GET, requestPath, "")) { 
    rest.execute(HOST, NULL, 0, false);
    bool reconnect;
    if(200==finishRequest(&reconnect)) {
      debug(F("Response body: "));debugln(response);
      // parse result from
      // {"id":7}
      char *stationIdStr = strstr_P(response, PSTR("id"))+2+1+1; // plus 2 for 'id', +1 for " and another for :
      char* end = strchr(stationIdStr,'}');
      end[0]=0;
      stationId = atoi(stationIdStr);
      debug(F("Station id is: "));debugln(stationId);
    }
  }
  free(requestPath);
  debug(F("getStationId end free RAM: "));debug(freeRam());debugln(F(" bytes."));
  return stationId;
}


/**
 * Report wind data
 * Must be in connected state SERVER for this to work
 */
bool reportObservation(int stationId, int direction, int speed, int minWindSpeed, int maxWindSpeed) {
  debug(F("reportObservation start free RAM: "));debug(freeRam());debugln(F(" bytes."));
  bool result = false;
  char *requestPath = (char *)malloc(14 + 1); // /observations/ + 0 
  strcpy_P(requestPath, PSTR("/observations/"));
  
  // 123456789012345678901234567890123
  // observation[station_id]=xxxxx      // 0-10000 - 39 characters
  // &observation[direction]=3600       // 0-3600  - 38 characters
  // &observation[speed]=nnnn           // 0-1000  - 34 characters
  // &observation[min_wind_speed]=nnnn  // 0-1000  - 43 characters
  // &observation[max_wind_speed]=nnnn  // 0-1000  - 43 characters
  
  time_t time;

  char *body = (char *)malloc(39 + 38 + 34 + 43 + 43 + 1); 
  strcpy_P(body, PSTR("observation[station_id]="));
  ultoa(stationId, &body[strlen(body)], 10);
  strcat_P(body, PSTR("&observation[direction]="));
  ultoa(direction, &body[strlen(body)], 10);
  strcat_P(body, PSTR("&observation[speed]="));
  ultoa(speed, &body[strlen(body)], 10);
  strcat_P(body, PSTR("&observation[min_wind_speed]="));
  ultoa(minWindSpeed, &body[strlen(body)], 10);
  strcat_P(body, PSTR("&observation[max_wind_speed]="));
  ultoa(maxWindSpeed, &body[strlen(body)], 10);  
    
  debug(F("Report observation "));debugln(body);
    
  if(initializeRequest(POST, requestPath, body, true)) {
    rest.execute(HOST, body, strlen(body), false);

    bool connectionClosed;
    int responseCode = finishRequest(&connectionClosed);
    if(201 == responseCode || 208 == responseCode ) {
      result = true;
    } else {
      result = false;
    }  
  } else {
    debugln(F("Failed to initialize report observation request"));
    result =  false;
  }
  free(requestPath);
  free(body);
  debug(F("reportObservation end free RAM: "));debug(freeRam());debugln(F(" bytes."));
  return result;
}


/**
 * Report firmware version
 * Must be in connected state SERVER for this to work
 */
bool reportFirmware(int stationId) {
  bool result = false;
  int stationIdLengthAsString = (stationId == 0 ? 1 : (int)(log10(stationId)+1));
  char *requestPath = (char *)malloc(9 + stationIdLengthAsString + 22 + 1); // /readers/ + reader id + /firmware_version.json + 0 
  strcpy_P(requestPath, PSTR("/stations/"));
  ultoa(stationId, &requestPath[strlen(requestPath)], 10);
  strcat_P(requestPath, PSTR("/firmware_version.json"));
  
  // 1234567890123456789012345678901234567890123456
  // station[firmware_version]=VERSION // 26 + length of VERSION
  // &station[gsm_software]=1137B10SIM900M64_ST_PZ // 23 + 22 = 45
  // '\0'
  char *body = (char *)malloc(26 + strlen(PSTR(VERSION)) + 45 + 1); 
  strcpy_P(body, PSTR("reader[firmware_version]="));
  strcat_P(body,PSTR(VERSION));
  strcat_P(body,PSTR("&reader[gsm_software]="));
  strcat(body, gsm.getSoftwareVersion());
  
  debug(F("Report firmware using body "));debug(F("("));debug(strlen(body));debug(F("):"));debugln(body);

  if(initializeRequest(PUT, requestPath, body, true)) {
    rest.execute(HOST, body, strlen(body), false);
    bool reconnect;
    if(200==finishRequest(&reconnect)) {
      result = true;
    }
  } else {
    debugln(F("Failed to initialize firmware report request"));
  }
  free(requestPath);
  free(body);
  return result;
}
