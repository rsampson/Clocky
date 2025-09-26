# Clocky
Desk clock using esp8266 / esp32 with web interface.

The clock will synchronize to a time server. The time zone is currently set to Los Angeles, setting to other time zones is a future feature. The clock will 
automatically correct for daylight savings time. The display shows day of week, date and month, and 12 hour time.

A piezo beeper is connected to D6 which serves as an hour chime and an alarm that can be set on the clock's web page. Omit if no chime or alarm is wanted. An Led is
connected to D4 trough a 100 ohm resistor. This gives the clock a "night lite" function. It may also be omited.  The clock has a nite light and sleep mode in
addition to a desk clock mode. Sleep mode will supress the lights as well as the hour chime.  A touch pad was implemented as an input to change the clock mode.
The touch pad may be any conductor, the conductor wires directly to the input D7 and also through a 300k resistor to output D3. Body capacitance will activate
the switching of the clock mode. The touch pad is not optional as the mode can't be changed without it.

The clock initiallly configures as a stand alone AP who's web page can be accessed at 192.168.4.1. Once the web page is accessed, your router ssid and password can 
be set up to put the clock on your lan. The clock will appear on your lan as "clocky.local". The web page can also be used to set the alarm and do a OTA firmware update.


