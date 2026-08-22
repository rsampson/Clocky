#include <Arduino.h>
#include <CircularBuffer.hpp>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "clocky.h"

#define BUFFER_SIZE 256

// Backs the "Log" section on the dashboard's Status tab. webPrint() is called
// throughout the firmware (boot message, alarm firing, Wi-Fi events, etc.)
// instead of Serial.print(), so those same messages show up over the web UI
// even when nobody has a serial cable plugged in.
CircularBuffer < char, (bufferSize - 4) > circBuff;

// printf-style logging: formats into a temporary buffer, echoes it to Serial
// as before, and appends it character-by-character into the circular buffer
// that fetchDebugText() later drains for the web UI.
void webPrint(const char* format, ...)
{
  char buffer[BUFFER_SIZE];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, BUFFER_SIZE, format, args);
  va_end(args);

  if(len < 0){
    return; // Error during formatting
  }
   Serial.print(buffer);
  // put formated string into circular buffer
  for (int i = 0; i < strlen(buffer); i++)
  {
    if(!circBuff.push(buffer[i])) // if data was overwritten
    {
      // Buffer is full: CircularBuffer::push() just silently overwrote the
      // oldest byte, which could leave a partial line at the front. Instead
      // of showing a garbled half-line, drop everything up to and including
      // the next newline, so what's left always starts at a line boundary.
      while(circBuff.first() != '\n') {
        circBuff.shift();
      }
      circBuff.shift(); // get rid of \n
    }
  }
}


// Copies the circular buffer's current contents into charBuf (see clocky.h)
// as a plain null-terminated string, for handleStatus() in web_server.cpp to
// send as the "log" field. Non-destructive: each byte is shift()ed off the
// front and immediately push()ed back on, so the buffer's contents and order
// are unchanged afterwards — this is a copy-out, not a drain.
void fetchDebugText() {
  int qty = circBuff.size();
  int i = 0;
  // unload ring buffer contents
  for (i = 0; i < qty; i++) {
    charBuf[i] = circBuff.shift();
    circBuff.push(charBuf[i]);
  }
  charBuf[i+1] = 0; //terminate string
}
