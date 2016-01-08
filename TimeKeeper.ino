#include <time.h>

void initTimekeeper() {
  setSyncProvider(retrieveTime);
  debugln(F("- TimeKeeper initialized"));
}

void sprintHttpDate(char *string, time_t time, bool urlEncode) {
// prints time into string according to RFC 2616, we use always teh rfc1123-date since it is common
//       HTTP-date    = rfc1123-date | rfc850-date | asctime-date
//       rfc1123-date = wkday "," SP date1 SP time SP "GMT"
//       rfc850-date  = weekday "," SP date2 SP time SP "GMT"
//       asctime-date = wkday SP date3 SP time SP 4DIGIT
//       date1        = 2DIGIT SP month SP 4DIGIT
//                      ; day month year (e.g., 02 Jun 1982)
//       date2        = 2DIGIT "-" month "-" 2DIGIT
//                      ; day-month-year (e.g., 02-Jun-82)
//       date3        = month SP ( 2DIGIT | ( SP 1DIGIT ))
//                      ; month day (e.g., Jun  2)
//       time         = 2DIGIT ":" 2DIGIT ":" 2DIGIT
//                      ; 00:00:00 - 23:59:59
//       wkday        = "Mon" | "Tue" | "Wed"
//                    | "Thu" | "Fri" | "Sat" | "Sun"
//       weekday      = "Monday" | "Tuesday" | "Wednesday"
//                    | "Thursday" | "Friday" | "Saturday" | "Sunday"
//       month        = "Jan" | "Feb" | "Mar" | "Apr"
//                    | "May" | "Jun" | "Jul" | "Aug"
//                    | "Sep" | "Oct" | "Nov" | "Dec"

  //DAY%2C%20DD%20MON%20YEAR%20HH%3AMM%3ASS%20GMT
  //012345678901234567890123456789012345678901234
  //DAY, DD MON YEAR HH:MM:SS GMT
  //01234567890123456789012345678
  strncpy(string, dayShortStr(dayOfWeek(time)), 3);
  
  uint8_t value = day(time);
  string[urlEncode?9:5]=value/10+'0';
  string[urlEncode?10:6]=value%10+'0';
  
  strncpy(&string[urlEncode?14:8], monthShortStr(month(time)), 3);
  
  uint16_t y = year(time);
  string[urlEncode?20:12]=y/1000+'0';
  string[urlEncode?21:13]=y/100%10+'0';
  string[urlEncode?22:14]=y/10%10+'0';
  string[urlEncode?23:15]=y%10+'0';
  
  value = hour(time);
  string[urlEncode?27:17]=value/10+'0';
  string[urlEncode?28:18]=value%10+'0';

  value = minute(time);
  string[urlEncode?32:20]=value/10+'0';
  string[urlEncode?33:21]=value%10+'0';
  
  value = second(time);
  string[urlEncode?37:23]=value/10+'0';
  string[urlEncode?38:24]=value%10+'0';
} 

void sprintHttpDate(char *string, bool urlEncode) {
  sprintHttpDate(string, now(), urlEncode);
} 

void printCurrentTime() {
  printTime(now());
}

void printTime(time_t time) {
  char* timeStr = "YYYY/MM/DD HH:MM:SS";
  sprintf(timeStr, "%4d/%02d/%02d %02d:%02d:%02d",year(time),month(time),day(time),hour(time),minute(time),second(time));
  debug(timeStr);
}
