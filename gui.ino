// ******************This is the main function which builds our GUI*******************

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

  timeLabel =    ESPUI.addControl(Label, "Time/Date", "", Wetasphalt, maintab, generalCallback);

  // change lable font size
  char styleBuff[30]; // temp buffer for css styles
  sprintf(styleBuff, "font-size: 25px;");
  ESPUI.setElementStyle(timeLabel, styleBuff);

  // bootLabel =    ESPUI.addControl(Label, "Boot Time", "", Wetasphalt, timeLabel, generalCallback);
  // bootTime = " boot up @ " + timeClient.getFormattedTime() + ", "  + Days[weekday()] ;  
  // ESPUI.updateLabel(bootLabel,  String(bootTime));

  signalLabel =  ESPUI.addControl(Label, "WiFi Signal Strength", "", Wetasphalt, maintab, generalCallback);
  ESPUI.setElementStyle(signalLabel, styleBuff);
  

  
  debugLabel =   ESPUI.addControl(Label, "Status/Debug", "some message", Wetasphalt, maintab, generalCallback);

  // This will recover time from the browser if not connected to the internet
  
   mainTime = ESPUI.addControl(Time, "", "", None, 0,
     [](Control *sender, int type) {
       if(type == TM_VALUE) { 
        //ESPUI.updateLabel(timeLabel, timeClient.getFormattedTime());
        ESPUI.updateLabel(timeLabel, sender->value);      }
   });


  /*
   * Tab: Alarm settings
   *-----------------------------------------------------------------------------------------------------------*/
 auto grouptab = ESPUI.addControl(Tab, "", "Set Alarm");
 
  //Number inputs also accept Min and Max components, but you should still validate the values.
  hourNumber = ESPUI.addControl(Number, "Run Hour", "12", Wetasphalt, grouptab, hourCallback);
  ESPUI.addControl(Min, "", "0", None, hourNumber);
  ESPUI.addControl(Max, "", "23", None, hourNumber);
  //Number inputs also accept Min and Max components, but you should still validate the values.
  minuteNumber = ESPUI.addControl(Number, "Run Minute", "0", Wetasphalt, grouptab, minuteCallback);
  ESPUI.addControl(Min, "", "0", None, minuteNumber);
  ESPUI.addControl(Max, "", "60", None, minuteNumber); 

  runHour = (stored_hour = preferences.getString("hour", "8")).toInt();
  runMinute= (stored_minute = preferences.getString("minute", "0")).toInt();

  ESPUI.updateNumber(hourNumber, stored_hour.toInt());
  ESPUI.updateNumber(minuteNumber, stored_minute.toInt());   
  
  ESPUI.addControl(Button, "Save Alarm Time", "Save", Wetasphalt, grouptab, SaveScheduleCallback);

  mainSwitcher = ESPUI.addControl(Switcher, "Activate Alarm", "0", Wetasphalt, grouptab, switchCallback);
  
  /*
   * Tab: WiFi Credentials
   * You use this tab to enter the SSID and password of a wifi network to autoconnect to.
   *-----------------------------------------------------------------------------------------------------------*/
  auto wifitab = ESPUI.addControl(Tab, "", "WiFi Credentials");
  wifi_ssid_text = ESPUI.addControl(Text, "SSID", "", Wetasphalt, wifitab, textCallback);
  //Note that adding a "Max" control to a text control sets the max length
  ESPUI.addControl(Max, "", "32", None, wifi_ssid_text);
  wifi_pass_text = ESPUI.addControl(Text, "Password", "", Wetasphalt, wifitab, textCallback);
  ESPUI.addControl(Max, "", "64", None, wifi_pass_text);
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