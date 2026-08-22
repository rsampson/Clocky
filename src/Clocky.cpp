/**
 * GUI adapted from the brilliant ESPUI written by: Lukas Bachschwell
 * and a demo by Ian Gray @iangray1000
 *
 * When this program boots, it will load an SSID and password from nvmem.
 * If these credentials do not work for some reason, the ESP will create an Access
 * Point wifi with the SSID HOSTNAME (defined in config.h). You can then connect and use
 * the controls on the "Wifi Credentials" tab to store credentials into the nvmem.
 *
 */
// Tested on ESP32 Wemos Lolin32  and ESP12-F (esp8266), make sure these match your board,
// otherwise strange results will occur.

#include "config.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Arduino.h>

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);   // SDA connects to D2, SCL  to D1 on the Wemos mini esp8266

bool buttonState;  // the current reading from the input pin
//bool lastButtonState = LOW;  // the previous reading from the input pin
unsigned int buttonPressCounter;  // how many times the button has been pressed

#if defined(ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#else

// esp8266
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

#include "wifi_manager.h"

#include <NTPClient.h>         // version 3.2.1 for ntp time client
#include <Timezone.h>          // https://github.com/JChristensen/Timezone
#include "time_manager.h"

#include <Preferences.h>
Preferences preferences;

#include <TimeLib.h>
#include <ElegantOTA.h>  // version 3.1.6 https://github.com/s00500/ESPUI

#include <CircularBuffer.hpp>         // version 1.4.0 https://github.com/rlogiacco/CircularBuffer
CircularBuffer<float, 24> dayBuffer;  // store 24 hour temp samples

#include <ESPUI.h>  // version 2.2.4  uses EsoAsyncWebServer 3.6.0, AsynchTCP version 3.35 WebSockets 2.6.1 and Arduinojson 6.21.5

#include "ui.h"
UI ui;

#include "clocky.h"

//Function Prototypes
void paramCallback(Control *sender, int type, int param);
void runAlarm(void);
bool readTouch(void);

//ESPUI=================================================================================================================

String stored_hour, stored_minute;

// Input values
uint16_t runHour = 2;     // hour to start running
uint16_t runMinute = 10;  // minute to start running

bool disable;
char charBuf[bufferSize];
String bootTime;

void setup() {

  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(DriverPin, OUTPUT);
  pinMode(SensorPin, INPUT);
  pinMode(ledPin, OUTPUT);

  tone(BUZZER_PIN, highTone, 1000);

  if (!preferences.begin("Settings")) {
    Serial.println(F("Failed to open preferences."));
    ESP.restart();
  }

  tz = TZstringToPointer(preferences.getString("tz", "UTC"));

  // Kick off the non-blocking WiFi state machine; handleWiFi() in loop()
  // drives the connection (including the very first attempt) from here.
  currentWifiState = WIFI_STATE_DISCONNECTED;

  timeClient.begin();  // set up ntp time client and then initialize time library
  setSyncProvider(getNtpTime);
  setSyncInterval(300);  // sync time server every 5 minutes

  dayBuffer.clear();

  Serial.println(F("configuring Gui"));
  setUpUI();

  disable = preferences.getBool("disable", "0");
  ESPUI.updateSwitcher(ui.mainSwitcher, disable);

  ElegantOTA.begin(ESPUI.WebServer());
  // boot up message
  webPrint("%s up at: %s on %s\n", HOSTNAME, timeClient.getFormattedTime(), dayStr(weekday()));
  // getBootReasonMessage(bootReasonMessage, BOOT_REASON_MESSAGE_SIZE);
  // webPrint("Reset reason: %s\n", bootReasonMessage);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {  // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }

  display.clearDisplay();

  display.setTextSize(1);               // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);  // Draw white text
  display.setFont(&FreeSans18pt7b);
  display.setCursor(0,28);               // Start at top-left corner
  display.println("booting");
  display.display();
  while(readTouch() == false){;}        // wait for touch to start telling time

  Serial.println(F("We Are Go!"));
}

unsigned int deltaTime;

void updateOledDisplay(void) {  // display mode set by push button

  char buf[20];

  display.clearDisplay();
  sprintf(buf, " ");
  //display.setTextColor(SSD1306_WHITE);
  //display.invertDisplay(true);
  int hr = hour() % 12;
  if (hr == 0) hr = 12;

  switch (buttonPressCounter % 2) {
    // case 0:                       // initial state
    //   digitalWrite(ledPin, LOW);  // turn on the night lite
    //   noTone(BUZZER_PIN);         // kill alarm
    //   sprintf(buf, " ");
    //   break;
    case 0:                        // clock display state
      digitalWrite(ledPin, LOW);  // turn on the night lite
      display.stopscroll();
      switch (second() % 5) {
        case 0:
          display.setFont(&FreeSans18pt7b);
          display.setCursor(0, 28);
          sprintf(buf, "   %s", dayShortStr(weekday()));
          break;
        case 1:  // display time 3 times longer than day of week or date
        case 2:
        case 3:
          display.setFont(&FreeSans24pt7b);
          display.setCursor(3, 31);
          sprintf(buf, "%2d:%02d", hr, minute());
          //sprintf(buf, "%3d", deltaTime);  // hack to debug touch switch
          break;
        case 4:
          display.setFont(&FreeSans12pt7b);
          display.setCursor(12, 25);
          sprintf(buf, " %s %2d", monthShortStr(month()), day());
          break;
      }
      break;
    case 1:  // sleep state
      digitalWrite(ledPin, HIGH) ;  // turn off the night lite
      noTone(BUZZER_PIN);         // kill alarm
      display.clearDisplay();
      //display.setTextColor(SSD1306_WHITE);
      //display.invertDisplay(false);
      display.setFont(&FreeSans12pt7b);
      display.setCursor(3, 16);
      sprintf(buf, "      .");
      display.startscrollright(0x00, 0x0F);
      break;
  }
  display.print(buf);
  display.display();
}

void runAlarm() {

  time_t t = now();  // Store the current time atomically

  int hr = hour(t) % 12;
  if (hr == 0) hr = 12;

  // process hourly chime if not in quiet mode
  if (minute(t) == 0 && second(t) == 0 && (buttonPressCounter % 2) != 1) {
    for (int i = 0; i < hr; i++) {
      tone(BUZZER_PIN, highTone, 150);  // c#7
      delay(300);
      tone(BUZZER_PIN, lowTone, 250);  // b6
      delay(700);
      noTone(BUZZER_PIN);
    }
    delay(400);  // prevent reentrance
  }

  if (disable) {  // bail out if alarm is disabled
    return;
  }
  // process alarm
  if (hour(t) == runHour && minute(t) == runMinute && second(t) == 0) {
    webPrint("Alarm run at: %d hour and %d minutes\n", runHour, runMinute);
    tone(BUZZER_PIN, highTone);
  }
}



bool readTouch(void) {  // read the state of the touch pad switch
  digitalWrite(DriverPin, LOW); // Initialize sensor pin to ground level
  pinMode(SensorPin, OUTPUT) ;
  digitalWrite(SensorPin, LOW);
  delay(10);
  digitalWrite(DriverPin, HIGH); // now drive the sensor pin high
  pinMode(SensorPin, INPUT);
  unsigned int bailOutCounter = 0;
  unsigned int timerVal = micros();
  while (digitalRead(SensorPin) != HIGH && bailOutCounter < 1000) { bailOutCounter++; }  // time how long it takes for the input to follow the output
  if (bailOutCounter == 1000) return (true);
  deltaTime = micros() - timerVal;
  //Serial.println(deltaTime);       // debug
  return (deltaTime >= treshold);   // If it takes longer than the treshold time to go high, declair that the pad is touched
}

long unsigned previousTime = 0;
bool initialTimeSynced = false;

void loop() {

  handleWiFi();         // non-blocking wifi connect/reconnect state machine
  timeClient.update();  // run ntp time client
  runAlarm();           // activate alarm if correct time
  ElegantOTA.loop();

  // setup() calls setSyncProvider() before WiFi exists, so that first sync
  // fails and TimeLib won't retry on its own for a full syncInterval. Force
  // one resync as soon as we have a connection so the clock is right at boot.
  if (!initialTimeSynced && currentWifiState == WIFI_STATE_CONNECTED) {
    initialTimeSynced = true;
    setTime(getNtpTime());
  }

  uint16_t trueSum = 0;
  for (int i = 0; i < 10; i++) {  // read pad 10 times and vote on it's state for debouncing
    if (readTouch() == true) trueSum++;
    delay(10);
  }
  bool reading = (trueSum > 6 ? HIGH : LOW);

  // if the button state has changed:
  if (reading != buttonState) {
    buttonState = reading;
    // only advance counter if the new button state is HIGH
    if (buttonState == HIGH) {
      buttonPressCounter++;
      tone(BUZZER_PIN, 2020, 30);
      delay(30);
      webPrint("Button press count: %02d\n", buttonPressCounter);
    }
  }
  // end button processing

  if (millis() > previousTime + 500) {  // update gui twice per second
    previousTime = millis();

    fetchDebugText();
    ESPUI.updateLabel(ui.debugLabel, String(charBuf));
    ESPUI.updateLabel(ui.signalLabel, String(WiFi.RSSI()) + " dbm");

    // determine if we are running stand alone to find the source of time (browser vs ntp server)
    if (ap_mode == false) {
      displayTime();      // update the web UI time/date label
      updateOledDisplay();  // update the physical OLED display
    } else {
      ESPUI.updateTime(ui.mainTime);  // get time from browser, we are not connect to the NTP server
    }
  }
#if !defined(ESP32)
  //We don't need to call this explicitly on ESP32 but we do on 8266
  MDNS.update();
#endif
}
