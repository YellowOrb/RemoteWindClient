extern SIM900GPRS gsm;

enum connected_state_t {
  NOT_CONNECTED,
  CONNECTED_TO_MOBILE_NETWORK,
  CONNECTED_TO_INTERNET,
  CONNECTED_TO_SERVER
};

connected_state_t connectionState;
