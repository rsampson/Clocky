/**
 * GUI adapted from the brilliant ESPUI written by: Lukas Bachschwell
 * and a demo by Ian Gray @iangray1000
 *
 * When this program boots, it will load an SSID and password from nvmem.
 * If these credentials do not work for some reason, the ESP will create an Access
 * Point wifi with the SSID HOSTNAME (defined below). You can then connect and use
 * the controls on the "Wifi Credentials" tab to store credentials into the nvmem.
 *
 */

// Tested on ESP32 Wemos Lolin32  and ESP12-F (esp8266), make sure these match your board,
// otherwise strange results will occur.
//Settings
#define HOSTNAME "clocky"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#include <Arduino.h>
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif
#define BUZZER_PIN  D6

// mode switch 
const int buttonPin = 14;  // the number of the pushbutton pin

int buttonState;            // the current reading from the input pin
int lastButtonState = LOW;  // the previous reading from the input pin
int buttonPressCounter;     // how many times the button has been pressed

// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers
// end mode switch


#if defined(ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#else

// esp8266
#define DEBUG true  //set to true for debug output, false for no debug output
#define Serial \
  if (DEBUG) Serial
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <umm_malloc/umm_heap_select.h>
#ifndef CORE_MOCK
#ifndef MMU_IRAM_HEAP
#warning Try MMU option '2nd heap shared' in 'tools' IDE menu (cf. https://arduino-esp8266.readthedocs.io/en/latest/mmu.html#option-summary)
#warning use decorators: { HeapSelectIram doAllocationsInIRAM; ESPUI.addControl(...) ... } (cf. https://arduino-esp8266.readthedocs.io/en/latest/mmu.html#how-to-select-heap)
#warning then check http://<ip>/heap
#endif  // MMU_IRAM_HEAP
#ifndef DEBUG_ESP_OOM
#error on ESP8266 and ESPUI, you must define OOM debug option when developping
#endif
#endif
#endif

#include <WiFiUdp.h>
WiFiUDP ntpUDP;

#include <NTPClient.h>         // version 3.2.1 for ntp time client
NTPClient timeClient(ntpUDP);  //https://github.com/arduino-libraries/NTPClient/blob/master/NTPClient.h
#include <Timezone.h>          // https://github.com/JChristensen/Timezone

#include <Preferences.h>
Preferences preferences;

#include <TimeLib.h>
#include <ElegantOTA.h>  // version 3.1.6 https://github.com/s00500/ESPUI

#include <CircularBuffer.hpp>         // version 1.4.0 https://github.com/rlogiacco/CircularBuffer
CircularBuffer<float, 24> dayBuffer;  // store 24 hour temp samples

#include <ESPUI.h>  // version 2.2.4  uses EsoAsyncWebServer 3.6.0, AsynchTCP version 3.35 WebSockets 2.6.1 and Arduinojson 6.21.5
//Function Prototypes
void connectWifi();
void setUpUI();
void textCallback(Control *sender, int type);
void generalCallback(Control *sender, int type);
void hourCallback(Control *sender, int type);
void minuteCallback(Control *sender, int type);
void SaveWifiDetailsCallback(Control *sender, int type);
void SaveSheduleCallback(Control *sender, int type);
void paramCallback(Control *sender, int type, int param);
void runAlarm(void);

//ESPUI=================================================================================================================

String stored_ssid, stored_pass, stored_hour, stored_minute;
//UI handles
uint16_t wifi_ssid_text, wifi_pass_text;
uint16_t debugLabel, timeLabel, signalLabel, bootLabel;
uint16_t mainSwitcher, mainText, mainTime, hourNumber, minuteNumber;

// Input values
uint16_t runHour = 2;     // hour to start running
uint16_t runMinute = 10;  // minute to start running
//ESPUI==================================================================================================================

const size_t bufferSize = 400;  // debug buffer
char charBuf[bufferSize];

bool disable = false;      // flag to disable/enable alarm

void getBootReasonMessage(char *buffer, int bufferlength) {
#if defined(ARDUINO_ARCH_ESP32)
  esp_reset_reason_t reset_reason = esp_reset_reason();

  switch (reset_reason) {
    case ESP_RST_UNKNOWN:
      snprintf(buffer, bufferlength, "Reset reason can not be determined");
      break;
    case ESP_RST_POWERON:
      snprintf(buffer, bufferlength, "Reset due to power-on event");
      break;
    case ESP_RST_SW:
      snprintf(buffer, bufferlength, "Software reset via esp_restart");
      break;
    case ESP_RST_PANIC:
      snprintf(buffer, bufferlength, "Software reset due to exception/panic");
      break;
    case ESP_RST_BROWNOUT:
      snprintf(buffer, bufferlength, "Brownout reset (software or hardware)");
      break;
    default:
      snprintf(buffer, bufferlength, "Unknown reset cause %d", reset_reason);
      break;
  }
#endif

#if defined(ARDUINO_ARCH_ESP8266)
  rst_info *resetInfo;
  resetInfo = ESP.getResetInfoPtr();

  switch (resetInfo->reason) {
    case REASON_DEFAULT_RST:
      snprintf(buffer, bufferlength, "Normal startup by power on");
      break;
    case REASON_WDT_RST:
      snprintf(buffer, bufferlength, "Hardware watch dog reset");
      break;
    case REASON_EXCEPTION_RST:
      snprintf(buffer, bufferlength, "Exception reset");
      break;
    case REASON_SOFT_WDT_RST:
      snprintf(buffer, bufferlength, "Software watch dog reset");
      break;
    case REASON_SOFT_RESTART:
      snprintf(buffer, bufferlength, "Software restart ,system_restart");
      break;
    case REASON_EXT_SYS_RST:
      snprintf(buffer, bufferlength, "External system reset");
      break;
    default:
      snprintf(buffer, bufferlength, "Unknown reset cause %d", resetInfo->reason);
      break;
  };
#endif
}

time_t getNtpTime(void){  // return time zone and DST adjusted time from server
  // US Pacific Time Zone (Las Vegas, Los Angeles)
  TimeChangeRule usPDT = { "PDT", Second, Sun, Mar, 2, -420 };
  TimeChangeRule usPST = { "PST", First, Sun, Nov, 2, -480 };
  Timezone usPT(usPDT, usPST);  
  time_t serv_time =   usPT.toLocal(timeClient.getEpochTime());
  return(serv_time);
}

#define BOOT_REASON_MESSAGE_SIZE 150
char bootReasonMessage[BOOT_REASON_MESSAGE_SIZE];
String bootTime;
char IP[] = "xxx.xxx.xxx.xxx";  // IP address string

void setup() {

  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);  // set heartbeat LED pin to OUTPUT
  digitalWrite(LED_BUILTIN, LOW);
  delay(50);
  digitalWrite(LED_BUILTIN, HIGH); 

  pinMode(BUZZER_PIN, OUTPUT);  
  tone(BUZZER_PIN, 415, 500);
  delay(500 * 1.3);
  tone(BUZZER_PIN, 466, 500);
  delay(500 * 1.3);
  tone(BUZZER_PIN, 370, 1000);
  delay(1000 * 1.3);
  noTone(BUZZER_PIN);

  pinMode(buttonPin, INPUT_PULLUP); 

  if (!preferences.begin("Settings")) {
    Serial.println("Failed to open preferences.");
    ESP.restart();
  }
  connectWifi();

  timeClient.begin();   // set up ntp time client and then initialize time library
  timeClient.update();
  setTime(getNtpTime());
  setSyncProvider(getNtpTime);
  setSyncInterval(300);  // sync time server every 5 minutes

  dayBuffer.clear();

  Serial.println("configuring Gui");
  setUpUI();

  disable = preferences.getBool("disable", "0");
  ESPUI.updateSwitcher(mainSwitcher, disable);
 
  ElegantOTA.begin(ESPUI.WebServer());
  // boot up message
  webPrint( "%s up at: %s on %s\n", HOSTNAME, timeClient.getFormattedTime(), dayStr(weekday()));
  getBootReasonMessage(bootReasonMessage, BOOT_REASON_MESSAGE_SIZE);
  webPrint("Reset reason: %s\n", bootReasonMessage);

   if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.setTextColor(WHITE);

  Serial.println("We Are Go!");
}

void displayTime(void) {  // display mode set by push button
  char buf[20];
  hourFormat12();

  sprintf(buf, "%02d:%02d:%02d %02d/%02d",  hour(), minute(), second(),  month(), day()); 
  ESPUI.updateLabel(timeLabel, buf);

  display.clearDisplay();
  display.setFont(&FreeSans24pt7b);
  display.setCursor(3, 31);

  switch (buttonPressCounter % 4) {
  case 0:
    display.setFont(&FreeSans18pt7b);
    display.setCursor(0, 25);
    sprintf(buf, "   %s", dayShortStr(weekday()) );     
    break;
  case 1:
    sprintf(buf, "%2d:%02d",  hour(), minute()); 
    break;
  case 2:
    display.setFont(&FreeSans18pt7b);
    display.setCursor(0, 25);
    sprintf(buf, " %s %2d",  monthShortStr(month()), day()); 
    break;
  case 3:
    sprintf(buf, "    ."); 
    display.startscrollright(0x00, 0x0F);
    break;
}
  display.print(buf);
  display.display(); 
}

void runAlarm() {

  if (disable) {
    return;
  }

  time_t t = now();   // Store the current time atomically
  if (hour(t) == runHour && minute(t) == runMinute && second(t) == 0 ) {   
    for( int j = 0; j < 20; j++) {
      tone(BUZZER_PIN, 2000, 100);
      delay(100);
      noTone(BUZZER_PIN);
    }
  }
}

long unsigned previousTime;
bool ap_mode = true;

void loop() {
  timeClient.update();  // run ntp time client
  runAlarm();  // activate alarm if correct time
  ElegantOTA.loop();

   // start button processing
  int reading = digitalRead(buttonPin);

    // If the switch changed, due to noise or pressing:
  if (reading != lastButtonState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != buttonState) {
      buttonState = reading;
      // only advance counter if the new button state is HIGH
      if (buttonState == HIGH) {
        display.stopscroll();
        buttonPressCounter++;
        previousTime -= 1000; // change display instantly
        webPrint("Button press count: %02d\n", buttonPressCounter);
      }
    }
  }

   // save the reading. Next time through the loop, it'll be the lastButtonState:
  lastButtonState = reading;
   // end button processing


  if (millis() > previousTime + 1000) {                    // update gui once per second

    fetchDebugText();
    ESPUI.updateLabel(debugLabel, String(charBuf));
    ESPUI.updateLabel(signalLabel, String(WiFi.RSSI()) + " dbm");

    // determine if we are running stand alone to find the source of time (browser vs ntp server)
    if (ap_mode == false) {
      displayTime();
    } else {
      ESPUI.updateTime(mainTime);  // get time from browser, we are not connect to the NTP server
    }

    previousTime = millis();
  }
#if !defined(ESP32)
  //We don't need to call this explicitly on ESP32 but we do on 8266
  MDNS.update();
#endif
}


void connectWifi() {
  int connect_timeout;

#if defined(ESP32)
  WiFi.setHostname(HOSTNAME);
#else
  WiFi.hostname(HOSTNAME);
#endif
  Serial.println("Begin wifi...");

  yield();

  stored_ssid = preferences.getString("ssid", "SSID");
  stored_pass = preferences.getString("pass", "PASSWORD");

  //Try to connect with stored credentials, fire up an access point if they don't work.
  Serial.println("Connecting to : " + stored_ssid);
#if defined(ESP32)
  WiFi.begin(stored_ssid.c_str(), stored_pass.c_str());
#else
  WiFi.begin(stored_ssid, stored_pass);
#endif
  connect_timeout = 28;  //7 seconds
  while (WiFi.status() != WL_CONNECTED && connect_timeout > 0) {
    delay(250);
    Serial.print(".");
    connect_timeout--;
  }

  if (WiFi.status() == WL_CONNECTED) {
    ap_mode = false;
    IPAddress ip = WiFi.localIP();  // display ip address
    ip.toString().toCharArray(IP, 16);
    webPrint("Wifi up, IP address = %s \n", IP);
    Serial.print(WiFi.RSSI());
    Serial.println(" dbm");

    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    if (!MDNS.begin(HOSTNAME)) {
      Serial.println("Error setting up MDNS responder!");
    }
      // Add service to MDNS-SD
      //MDNS.addService("http", "tcp", 80);
  } else {
    ap_mode = true;
    Serial.println("\nCreating access point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(HOSTNAME);
  }
#if defined(ESP32)
  WiFi.setSleep(false);  //For the ESP32: turn off sleeping to increase UI responsivness (at the cost of power use)
#endif
}
