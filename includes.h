extern SIM900GPRS gsm;

enum connected_state_t {
  NOT_CONNECTED,
  CONNECTED_TO_MOBILE_NETWORK,
  CONNECTED_TO_INTERNET,
  CONNECTED_TO_SERVER
};

connected_state_t connectionState;

//enum station_state_t {
 // IDLING,
//  VERIFYING_TAG,
//  START_SEQUENCE,
//  WAIT_FOR_USER_TO_START
//};


//enum reader_kind_t {
//  UNDEFINED,
//  START,
//  INTERMEDIATE,
//  FINISH
//};

//enum storage_status_t {
//  LAST = 1,
//  OK = 2,
//  EMPTY = 4,
//  FAULTY = 8
//};
