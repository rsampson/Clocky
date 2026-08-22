// ******************This is the main function which builds our GUI*******************

#include <Arduino.h>
#include <ESPUI.h>

#if defined(ESP8266)
#include <umm_malloc/umm_heap_select.h>
#endif

#include "clocky.h"
#include "config.h"

void setUpUI() {

#ifdef ESP8266
    { HeapSelectIram doAllocationsInIRAM;
#endif

  //Turn off verbose debugging
  ESPUI.setVerbosity(Verbosity::Quiet);

  /*
   * Tab: Basic Controls
   * This tab contains all the basic ESPUI controls, and shows how to read and update them at runtime.
   *-----------------------------------------------------------------------------------------------------------*/
  auto maintab = ESPUI.addControl(Tab, "", "System Status");

  ui.timeLabel =    ESPUI.addControl(Label, "Time/Date", "", Wetasphalt, maintab, generalCallback);

  // change lable font size
  char styleBuff[30]; // temp buffer for css styles
  sprintf(styleBuff, "font-size: 25px;");
  ESPUI.setElementStyle(ui.timeLabel, styleBuff);

  // ui.bootLabel =    ESPUI.addControl(Label, "Boot Time", "", Wetasphalt, ui.timeLabel, generalCallback);
  // bootTime = " boot up @ " + timeClient.getFormattedTime() + ", "  + Days[weekday()] ;
  // ESPUI.updateLabel(ui.bootLabel,  String(bootTime));

  ui.signalLabel =  ESPUI.addControl(Label, "WiFi Signal Strength", "", Wetasphalt, maintab, generalCallback);
  ESPUI.setElementStyle(ui.signalLabel, styleBuff);



  ui.debugLabel =   ESPUI.addControl(Label, "Status/Debug", "some message", Wetasphalt, maintab, generalCallback);

  // This will recover time from the browser if not connected to the internet

   ui.mainTime = ESPUI.addControl(Time, "", "", None, 0,
     [](Control *sender, int type) {
       if(type == TM_VALUE) {
        //ESPUI.updateLabel(ui.timeLabel, timeClient.getFormattedTime());
        ESPUI.updateLabel(ui.timeLabel, sender->value);      }
   });


  /*
   * Tab: Alarm settings
   *-----------------------------------------------------------------------------------------------------------*/
 auto grouptab = ESPUI.addControl(Tab, "", "Set Alarm");

  //Number inputs also accept Min and Max components, but you should still validate the values.
  ui.hourNumber = ESPUI.addControl(Number, "Run Hour", "12", Wetasphalt, grouptab, hourCallback);
  ESPUI.addControl(Min, "", "0", None, ui.hourNumber);
  ESPUI.addControl(Max, "", "23", None, ui.hourNumber);
  ui.minuteNumber = ESPUI.addControl(Number, "Run Minute", "0", Wetasphalt, grouptab, minuteCallback);
  ESPUI.addControl(Min, "", "0", None, ui.minuteNumber);
  ESPUI.addControl(Max, "", "60", None, ui.minuteNumber);

  runHour = (stored_hour = preferences.getString("hour", "8")).toInt();
  runMinute= (stored_minute = preferences.getString("minute", "0")).toInt();

  ESPUI.updateNumber(ui.hourNumber, stored_hour.toInt());
  ESPUI.updateNumber(ui.minuteNumber, stored_minute.toInt());

  ESPUI.addControl(Button, "Save Alarm Time", "Save", Wetasphalt, grouptab, SaveScheduleCallback);

  ui.mainSwitcher = ESPUI.addControl(Switcher, "Activate Alarm", "0", Wetasphalt, grouptab, switchCallback);

  /*
   * Tab: WiFi Credentials
   * You use this tab to enter the SSID and password of a wifi network to autoconnect to.
   *-----------------------------------------------------------------------------------------------------------*/
  auto wifitab = ESPUI.addControl(Tab, "", "WiFi Credentials");
  ui.wifi_ssid_text = ESPUI.addControl(Text, "SSID", "", Wetasphalt, wifitab, textCallback);
  //Note that adding a "Max" control to a text control sets the max length
  ESPUI.addControl(Max, "", "32", None, ui.wifi_ssid_text);
  ui.wifi_pass_text = ESPUI.addControl(Text, "Password", "", Wetasphalt, wifitab, textCallback);
  ESPUI.addControl(Max, "", "64", None, ui.wifi_pass_text);
  ESPUI.addControl(Button, "Save", "Save", Wetasphalt, wifitab, SaveWifiDetailsCallback);

  /*
   * Tab:System Maintenance
   * You use this tab to upload new code OTA, see ElegantOTA library doc
   *-----------------------------------------------------------------------------------------------------------*/
   auto maintenancetab = ESPUI.addControl(Tab, "", "System Maintenance");
   auto updateButton =   ESPUI.addControl(Label, "Code Update", "<a href=\"/update\"> <button>Update</button></a>", Wetasphalt, maintenancetab, generalCallback);
    //We need this CSS style rule, which will remove the label's background and ensure that it takes up the entire width of the panel
   String clearLabelStyle = "background-color: unset; width: 100%;";
   ESPUI.setElementStyle(updateButton , clearLabelStyle);
   ESPUI.addControl(Button, "", "Reboot",  Wetasphalt,  updateButton, ESPReset);

  /*
   * Timezone selection, on the System Maintenance tab.
   *-----------------------------------------------------------------------------------------------------------*/
  ui.timeZoneSelect = ESPUI.addControl(ControlType::Select, "Select Time Zone", "", ControlColor::Wetasphalt, maintenancetab, &TZcallback);
  ESPUI.addControl(ControlType::Option, "Australia Eastern Time", "AEST", ControlColor::Wetasphalt, ui.timeZoneSelect);
  ESPUI.addControl(ControlType::Option, "Moscow", "MSK", ControlColor::Wetasphalt, ui.timeZoneSelect);
  ESPUI.addControl(ControlType::Option, "Central European Time", "CE", ControlColor::Wetasphalt, ui.timeZoneSelect);
  ESPUI.addControl(ControlType::Option, "British Standard Time", "GMT", ControlColor::Wetasphalt, ui.timeZoneSelect);
  ESPUI.addControl(ControlType::Option, "Universal Time", "UTC", ControlColor::Wetasphalt, ui.timeZoneSelect);
  ESPUI.addControl(ControlType::Option, "Eastern Standard Time", "EST", ControlColor::Wetasphalt, ui.timeZoneSelect);
  ESPUI.addControl(ControlType::Option, "Central Standard Time", "CST", ControlColor::Wetasphalt, ui.timeZoneSelect);
  ESPUI.addControl(ControlType::Option, "Mountain Standard Time", "MST", ControlColor::Wetasphalt, ui.timeZoneSelect);
  ESPUI.addControl(ControlType::Option, "Arizona Time", "AZT", ControlColor::Wetasphalt, ui.timeZoneSelect);
  ESPUI.addControl(ControlType::Option, "Pacific Standard Time", "PST", ControlColor::Wetasphalt, ui.timeZoneSelect);

  ui.timeZoneLabel = ESPUI.addControl(Label, "Currently Selected Time Zone", "", ControlColor::Wetasphalt, maintenancetab);
  ESPUI.updateSelect(ui.timeZoneSelect, preferences.getString("tz", "UTC"));
  printTZ(tz);

  // *********how to add an extended web page**********
//  ESPUI.WebServer()->on("/narf", HTTP_GET, [](AsyncWebServerRequest *request) {
//  request->send(200, "text/html", "<A HREF = \"http://192.168.0.74:8080/\">Rear Controller</A>");
//  });

  //Finally, start up the UI.
  //char title[] = " Garden Watering System";
  //This should only be called once we are connected to WiFi.
  ESPUI.begin(HOSTNAME);


#ifdef ESP8266
    } // HeapSelectIram
#endif

}