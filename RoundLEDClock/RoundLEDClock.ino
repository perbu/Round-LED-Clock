/*
  WiFi connected round LED Clock. It gets NTP time from the internet and translates to a 60 RGB WS2812B LED strip.

  If you have another orientation where the wire comes out then change the methods getLEDHour and getLEDMinuteOrSecond

  Happy programming, Leon van den Beukel, march 2019

  ---  
  NTP and summer time code based on:
  https://tttapa.github.io/ESP8266/Chap15%20-%20NTP.html 
  https://github.com/SensorsIot/NTPtimeESP/blob/master/NTPtimeESP.cpp (for US summer time support check this link)  
  
*/

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiUdp.h>
#include <FastLED.h>
#define DEBUG_ON

const char ssid[] = "*";        // Your network SSID name here
const char pass[] = "*";        // Your network password here
unsigned long timeZone = 1.0;   // Change this value to your local timezone (in my case +1 for Amsterdam)

// Change the colors here if you want.
// Check for reference: https://github.com/FastLED/FastLED/wiki/Pixel-reference#predefined-colors-list
// You can also set the colors with RGB values, for example red:
// CRGB colorHour = CRGB(255, 0, 0);
CRGB colorHour = CRGB::Red;
CRGB colorMinute = CRGB::Green;
CRGB colorSecond = CRGB::Blue;

ESP8266WiFiMulti wifiMulti;                     
WiFiUDP UDP;                                    
IPAddress timeServerIP;                         
const char* NTPServerName = "time.nist.gov";    
const int NTP_PACKET_SIZE = 48;                 
byte NTPBuffer[NTP_PACKET_SIZE];                

unsigned long intervalNTP = 5 * 60000; // Request NTP time every 5 minutes
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();
uint32_t timeUNIX = 0;
unsigned long prevActualTime = 0;

#define LEAP_YEAR(Y) ( ((1970+Y)>0) && !((1970+Y)%4) && ( ((1970+Y)%100) || !((1970+Y)%400) ) )
static const uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

#define NUM_LEDS 60     
#define DATA_PIN D6
CRGB LEDs[NUM_LEDS];

struct DateTime {
  int  year;
  byte month;
  byte day;
  byte hour;
  byte minute;
  byte second;
  byte dayofweek;
};

DateTime currentDateTime;

void setup() {

  FastLED.delay(3000);
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(LEDs, NUM_LEDS);  

  Serial.begin(115200);          
  delay(10);
  Serial.println("\r\n");

  startWiFi();
  startUDP();

  if(!WiFi.hostByName(NTPServerName, timeServerIP)) { 
    Serial.println("DNS lookup failed. Rebooting.");
    Serial.flush();
    ESP.reset();
  }
  Serial.print("Time server IP:\t");
  Serial.println(timeServerIP);
  
  Serial.println("\r\nSending NTP request ...");
  sendNTPpacket(timeServerIP);  
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - prevNTP > intervalNTP) { // If a minute has passed since last NTP request
    prevNTP = currentMillis;
    Serial.println("\r\nSending NTP request ...");
    sendNTPpacket(timeServerIP);               // Send an NTP request
  }

  uint32_t time = getTime();                   // Check if an NTP response has arrived and get the (UNIX) time
  if (time) {                                  // If a new timestamp has been received
    timeUNIX = time;
    Serial.print("NTP response:\t");
    Serial.println(timeUNIX);
    lastNTPResponse = currentMillis;
  } else if ((currentMillis - lastNTPResponse) > 3600000) {
    Serial.println("More than 1 hour since last NTP response. Rebooting.");
    Serial.flush();
    ESP.reset();
  }

  uint32_t actualTime = timeUNIX + (currentMillis - lastNTPResponse)/1000;
  if (actualTime != prevActualTime && timeUNIX != 0) { // If a second has passed since last update
    prevActualTime = actualTime;
    convertTime(actualTime);

    for (int i=0; i<NUM_LEDS; i++) 
      LEDs[i] = CRGB::Black;

    LEDs[getLEDMinuteOrSecond(currentDateTime.second)] = colorSecond;
    LEDs[getLEDHour(currentDateTime.hour)] = colorHour;
    LEDs[getLEDMinuteOrSecond(currentDateTime.minute)] = colorMinute;    

    FastLED.show();
  }  
}

byte getLEDHour(byte hours) {
  if (hours > 12)
    hours = hours - 12;

  if (hours <= 5) 
    return (hours * 5) + 30;
  else
    return (hours * 5) - 30;
}

byte getLEDMinuteOrSecond(byte minuteOrSecond) {
  if (minuteOrSecond < 30) 
    return minuteOrSecond + 30;
  else 
    return minuteOrSecond - 30;
}

void startWiFi() { 
  wifiMulti.addAP(ssid, pass);   

  Serial.println("Connecting");
  byte i = 0;
  while (wifiMulti.run() != WL_CONNECTED) {  
    delay(250);
    Serial.print('.');
    LEDs[i++] = CRGB::Green;
    FastLED.show();    
  }
  Serial.println("\r\n");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());             
  Serial.print("IP address:\t");
  Serial.print(WiFi.localIP());            
  Serial.println("\r\n");
}

void startUDP() {
  Serial.println("Starting UDP");
  UDP.begin(123);                          // Start listening for UDP messages on port 123
  Serial.print("Local port:\t");
  Serial.println(UDP.localPort());
  Serial.println();
}

uint32_t getTime() {
  if (UDP.parsePacket() == 0) { // If there's no response (yet)
    return 0;
  }
  UDP.read(NTPBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  // Combine the 4 timestamp bytes into one 32-bit number
  uint32_t NTPTime = (NTPBuffer[40] << 24) | (NTPBuffer[41] << 16) | (NTPBuffer[42] << 8) | NTPBuffer[43];
  // Convert NTP time to a UNIX timestamp:
  // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  const uint32_t seventyYears = 2208988800UL;
  // subtract seventy years:
  uint32_t UNIXTime = NTPTime - seventyYears;
  return UNIXTime;
}

void sendNTPpacket(IPAddress& address) {
  memset(NTPBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
  // Initialize values needed to form NTP request
  NTPBuffer[0] = 0b11100011;   // LI, Version, Mode
  // send a packet requesting a timestamp:
  UDP.beginPacket(address, 123); // NTP requests are to port 123
  UDP.write(NTPBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}

void convertTime(uint32_t time) {
  // Correct time zone
  time += (3600 * timeZone);
  
  currentDateTime.second = time % 60;
  currentDateTime.minute = time / 60 % 60;
  currentDateTime.hour   = time / 3600 % 24;
  time  /= 60;  // To minutes
  time  /= 60;  // To hours
  time  /= 24;  // To days
  currentDateTime.dayofweek = ((time + 4) % 7) + 1;
  int year = 0;
  int days = 0;
  while ((unsigned)(days += (LEAP_YEAR(year) ? 366 : 365)) <= time) {
    year++;
  }
  days -= LEAP_YEAR(year) ? 366 : 365;
  time  -= days; // To days in this year, starting at 0  
  days = 0;
  byte month = 0;
  byte monthLength = 0;
  for (month = 0; month < 12; month++) {
    if (month == 1) { // February
      if (LEAP_YEAR(year)) {
        monthLength = 29;
      } else {
        monthLength = 28;
      }
    } else {
      monthLength = monthDays[month];
    }
  
    if (time >= monthLength) {
      time -= monthLength;
    } else {
      break;
    }
  }
 
  currentDateTime.day = time + 1;
  currentDateTime.year = year + 1970;
  currentDateTime.month = month + 1;  

  // Correct European Summer time
  if (summerTime()) {
    currentDateTime.hour += 1;
  }

#ifdef DEBUG_ON
  Serial.print(currentDateTime.year);
  Serial.print(" ");
  Serial.print(currentDateTime.month);
  Serial.print(" ");
  Serial.print(currentDateTime.day);
  Serial.print(" ");
  Serial.print(currentDateTime.hour);
  Serial.print(" ");
  Serial.print(currentDateTime.minute);
  Serial.print(" ");
  Serial.print(currentDateTime.second);
  Serial.print(" day of week: ");
  Serial.print(currentDateTime.dayofweek);
  Serial.print(" summer time: ");
  Serial.print(summerTime());
  Serial.println();
#endif  
}

boolean summerTime() {

  if (currentDateTime.month < 3 || currentDateTime.month > 10) return false;  // No summer time in Jan, Feb, Nov, Dec
  if (currentDateTime.month > 3 && currentDateTime.month < 10) return true;   // Summer time in Apr, May, Jun, Jul, Aug, Sep
  if (currentDateTime.month == 3 && (currentDateTime.hour + 24 * currentDateTime.day) >= (3 +  24 * (31 - (5 * currentDateTime.year / 4 + 4) % 7)) || currentDateTime.month == 10 && (currentDateTime.hour + 24 * currentDateTime.day) < (3 +  24 * (31 - (5 * currentDateTime.year / 4 + 1) % 7)))
  return true;
    else
  return false;
}
