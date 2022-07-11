# oclock
Revision of Hans Andersson's o-clock code on instructables at: https://www.instructables.com/O-Clock/

I made the following changes:

1. Switched from FastLED to neopixelbus. (FastLED has a bug with flashing LEDs when run on a Wemos D1 Mini.)
2. Added OTA update capability.
3. Retrieve current time from NTP server.
4. Added HTTP server for setting of clock mode.
