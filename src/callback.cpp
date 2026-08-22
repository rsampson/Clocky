#include <Arduino.h>
#include <ESPUI.h>

#include "clocky.h"
#include "time_manager.h"
#include "wifi_manager.h"

//Most elements in this test UI are assigned this generic callback which prints some
//basic information. Event types are defined in ESPUI.h
void generalCallback(Control *sender, int type) {
  Serial.print("CB: id(");
  Serial.print(sender->id);
  Serial.print(") Type(");
  Serial.print(type);
  Serial.print(") '");
  Serial.print(sender->label);
  Serial.print("' = ");
  Serial.println(sender->value);
}

// The extended param can be used to pass additional information
void paramCallback(Control* sender, int type, int param)
{
  generalCallback(sender, type);
  Serial.print("param = ");
  Serial.println(param);
}

void textCallback(Control *sender, int type) {
  //This callback is needed to handle the changed values, even though it doesn't do anything itself.
}


//Alarm schedule settings callback========================================
void SaveScheduleCallback(Control *sender, int type) {
  if (type == B_UP) {
    // store run hour and minute
    stored_hour =   ESPUI.getControl(ui.hourNumber)->value;
    stored_minute = ESPUI.getControl(ui.minuteNumber)->value;

    runHour = stored_hour.toInt();
    runMinute= stored_minute.toInt();
        
    preferences.putString("hour", stored_hour);
    preferences.putString("minute", stored_minute);

    webPrint("Saved %s alarm hour: %d and minute: %d\n", disable ? "disabled" : "active", runHour, runMinute);
  }
}
//END: Alarm schedule settings settings callback===============================


void hourCallback(Control *sender, int type) {
  if(type == N_VALUE) { 
      webPrint("Alarm hour set: %s \n",(sender->value).c_str() );
      runHour =  (sender->value).toInt();
  }
   generalCallback(sender, type);
}

void minuteCallback(Control *sender, int type) {
   if(type == N_VALUE) { 
      webPrint("Alarm minute set: %s \n",(sender->value).c_str() );
      runMinute =  (sender->value).toInt();
  }
   generalCallback(sender, type);
}

void switchCallback(Control* sender, int type)
{
    switch (type)
    {
    case S_INACTIVE:
        disable = true;
        preferences.putBool("disable", true);
        break;

    case S_ACTIVE:
        disable = false;
        preferences.putBool("disable", false);
        break;
    }
    Serial.println(disable ? "disable" : "active");
   generalCallback(sender, type);
}

//WiFi settings callback=====================================================
void SaveWifiDetailsCallback(Control *sender, int type) {
  if (type == B_UP) {
    stored_ssid = ESPUI.getControl(ui.wifi_ssid_text)->value;
    stored_pass = ESPUI.getControl(ui.wifi_pass_text)->value;

    preferences.putString("ssid", stored_ssid);
    preferences.putString("pass", stored_pass);

    // apply immediately and let handleWiFi() reconnect with the new credentials
    currentWifiState = WIFI_STATE_DISCONNECTED;
  }
}
//WiFi settings callback=====================================================


//Timezone selection callback=================================
void TZcallback(Control *sender, int type) {
  tz = TZstringToPointer(sender->value);
  preferences.putString("tz", sender->value);
  printTZ(tz);
  setTime(getNtpTime());

  generalCallback(sender, type);
}
//Timezone selection callback=================================


//ESP Reset=================================
void ESPReset(Control *sender, int type) {
  if (type == B_UP) {
    ESP.restart();
  }
}
//ESP Reset=================================
