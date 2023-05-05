#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoOTA.h>
#include <NeoPixelBrightnessBus.h>
#include <math.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <NTP.h>

typedef uint8_t   fract8;

#define VERSION "0.01"

#define PHOTO_RESISTOR_PIN A0
#define LED_PIN     3
#define NUM_LEDS    60
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define FRAMES_PER_SECOND 50
#define MAX_TINT 80
#define MAX_HUE_OFFSET 25
#define MIN_BRIGHTNESS 7
#define MAX_BRIGHTNESS 255
#define MIN_LIGHT 400
#define MAX_LIGHT 900
#define LIGHT_SENSITIVITY 500  //minimum light for light sensor to work as a button
#define NUM_MODES  6
#define MODE_SIMPLE  0
#define MODE_MINIMALISTIC 1
#define MODE_PROGRESS  2
#define MODE_SWEEP  3
#define MODE_SPECTRUM 4
#define MODE_HYSTERICAL 5

static int ledBrightness = 255;
static int displayMode = MODE_SIMPLE;

#define WIFI_SSID "" // your WiFi's SSID
#define WIFI_PASS "" // your WiFi's password
const char *OTA_HOSTNAME = "OCLOCK-";
const String OTA_Password = "";     // Set an OTA password here -- leave blank if you don't want to be prompted for password
char* www_username = "admin";  // User account for the Web Interface
char* www_password = "password";  // Password for the Web Interface
const int WEBSERVER_PORT = 80; // The port you can access this device on over HTTP

const int     timeZoneId = 5;
const unsigned int timePort = 2390;
const unsigned long ntpInterval = 3600000;  // time between calls to NIST server
WiFiUDP udpTime;
NTP timeClient(udpTime);
const int timeZoneOffset = -8;
const boolean observeDST = true;

const float HUE_RED = 0.0;
const float HUE_ORANGE = 30.0 / 360.0;
const float HUE_YELLOW = 60.0 / 360.0;
const float HUE_GREEN = 120.0 / 360.0;
const float HUE_AQUA = 180.0 / 360.0;
const float HUE_BLUE = 240.0 / 360.0;
const float HUE_PURPLE = 270.0 / 360.0;
const float HUE_PINK = 300.0 / 360.0;


//  https://www.instructables.com/O-Clock/

ESP8266WebServer server(WEBSERVER_PORT);
ESP8266HTTPUpdateServer serverUpdater;
NeoPixelBrightnessBus<NeoGrbFeature, Neo800KbpsMethod> strip(NUM_LEDS);

void setup() {
  Serial.begin(115200);
  delay( 3000 ); // power-up safety delay

  strip.Begin();
  strip.Show();
  initWiFi();
  initOTA();
  initWebServer();
  initTime();
}

void loop() {
  static unsigned long old_s = 0;
  static int old_light = 0;
  static unsigned long last_dimmed = 0;
  static int tint_a = 0;
  static int tint_b = 0;
  static int tintSteps = 3;
  static int hueOffset = 0;
  static int hueSteps = 1;

  unsigned long h = hour(getTime(true));
  unsigned long m = minute(getTime(true));
  unsigned long s = second(getTime(true));

  if (s != old_s) {
    old_s = s;

    int light = analogRead(PHOTO_RESISTOR_PIN);
    Serial.printf("light level is %d\n", light);
    if (light < 100 && old_light > LIGHT_SENSITIVITY) {
      last_dimmed = millis();
    } else if (light > LIGHT_SENSITIVITY && millis() - last_dimmed < 3000) {
      displayMode = (displayMode + 1) % NUM_MODES;
      last_dimmed = 0;
    }
    old_light = light;
    light = constrain(light, MIN_LIGHT, MAX_LIGHT);
    int b1 = map(light, MIN_LIGHT, MAX_LIGHT, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
    int b2 = strip.GetBrightness();
    if (b1 != b2 && (b1 == MIN_BRIGHTNESS || b1 == MAX_BRIGHTNESS || abs(b1 - b2) * 100 / b2 > 10)) {
      strip.SetBrightness(b1);
      Serial.printf("Set brightness to %d\n");
      ledBrightness = b1;
    }

    int h_led = h % 12 * 5 + m / 12;
    int m_led = m;
    int s_led = s;

    hueOffset += hueSteps;
    if (abs(hueOffset) == MAX_HUE_OFFSET) {
      hueSteps *= -1;
    }
    if (tint_a == 0) {
      tint_b += tintSteps;
      tint_b = constrain(tint_b, 0, MAX_TINT);
      if (tint_b == 0 || tint_b == MAX_TINT) {
        tintSteps *= -1;
      }
      if (tint_b == 0) {
        tint_a = 1;
      }
    } else {
      tint_a += tintSteps;
      tint_a = constrain(tint_a, 0, MAX_TINT);
      if (tint_a == 0 || tint_a == MAX_TINT) {
        tintSteps *= -1;
      }
    }

    switch (displayMode) {
      case MODE_SIMPLE:
        modeSimple(h_led, m_led, s_led, tint_a, tint_b);
        break;
      case  MODE_MINIMALISTIC:
        modeMinimalistic(h_led, m_led, tint_a, tint_b);
        break;
      case MODE_PROGRESS:
        modeProgress(h_led, m_led, s_led, tint_a, tint_b);
        break;
      case MODE_SWEEP:
        modeSweep(h_led, m_led, s_led, hueOffset);
        break;
      case MODE_SPECTRUM:
        modeSpectrum(h_led, m_led, s_led, tint_a, tint_b);
        break;
      case MODE_HYSTERICAL:
        modeHysterical(h_led, m_led, s_led, hueOffset);
        break;
      default:
        modeSimple(h_led, m_led, s_led, tint_a, tint_b);
        break;
    }
  }
  server.handleClient();
  ArduinoOTA.handle();
}


void modeSimple(int h, int m, int s, int tint_a, int tint_b) {
  RgbColor hColor = RgbColor(255, tint_a, tint_b);
  RgbColor mColor = RgbColor(tint_b, 255, tint_a);
  RgbColor sColor = RgbColor(tint_a, tint_b, 255);

  strip.ClearTo(RgbColor(0, 0, 0));

  strip.SetPixelColor(h, hColor);
  strip.SetPixelColor(m, mColor);
  strip.SetPixelColor(s, sColor);

  setTimePointColors( h, m,  s,  hColor,  mColor,  sColor);
  strip.Show();
}


void modeMinimalistic(int h, int m, int tint_a, int tint_b) {
  RgbColor hColor = RgbColor(255, tint_a, tint_b);
  RgbColor mColor = RgbColor(tint_b, 255, tint_a);

  strip.ClearTo(RgbColor(0, 0, 0));

  strip.SetPixelColor(h, hColor);
  strip.SetPixelColor(m, mColor);

  setTimePointColors( h, m,  -1,  hColor,  mColor,  0);

  strip.Show();
}

void setTimePointColors(int h, int m, int s, RgbColor hColor, RgbColor mColor, RgbColor sColor) {
  if (s == m) {
    if (m == h) {
      strip.SetPixelColor(s, RgbColor::LinearBlend(mColor, sColor, 0.5));
    } else {
      strip.SetPixelColor(s, RgbColor::LinearBlend(mColor, sColor, 0.5));
    }
  } else if (s == h) {
    strip.SetPixelColor(s, RgbColor::LinearBlend(hColor, sColor, 0.5));
  }  else if (h == m) {
    strip.SetPixelColor(m, RgbColor::LinearBlend(hColor, mColor, 0.5));
  }
}

void modeProgress(int h, int m, int s, int tint_a, int tint_b) {
  RgbColor hColor = RgbColor(255, tint_a, tint_b);
  RgbColor mColor = RgbColor(tint_b, 255, tint_a);
  RgbColor sColor = RgbColor(tint_a, tint_b, 255);
  strip.ClearTo(RgbColor(0, 0, 0));
  //  FastLED.clear();
  for (int i = h; i >= 0 && (i == h || (i != m && i != s)); i--) {
    strip.SetPixelColor(i, hColor);
  }
  for (int i = m; i >= 0 && (i == m || (i != h && i != s)); i--) {
    strip.SetPixelColor(i, mColor);
  }
  for (int i = s; i >= 0 && (i == s || (i != h && i != m)); i--) {
    strip.SetPixelColor(i, sColor);
  }

  setTimePointColors(h, m, s, hColor, mColor, sColor);

  strip.Show();
}


void modeSweep(int h, int m, int s, int hueOffset) {
  strip.ClearTo(RgbColor(0, 0, 0));

  for (int i = 1; i < 15 && (h - i + 60) % 60 != m && (h - i + 60) % 60 != s; i++) {
    Serial.printf("hour brightness<%f>\n", (255.0 - 15.0 * (float)i) / 255.0);
    //    strip.SetPixelColor((h - i + 60) % 60, HsbColor(HUE_RED, 1.0, (255.0 - 15.0 * (float)i)/255.0));//    1.0 - (float)i/16.0)); //(255 - 15 * i));
    strip.SetPixelColor((h - i + 60) % 60, RgbColor(255 - (15 * i), 0, 0));//    1.0 - (float)i/16.0)); //(255 - 15 * i));
  }

  for (int i = 1; i < 15 && (m - i + 60) % 60 != h && (m - i + 60) % 60 != s; i++) {
    Serial.printf("min brightness<%f>\n", (255.0 - 15.0 * (float)i) / 255.0);
    strip.SetPixelColor((m - i + 60) % 60, HsbColor(HUE_GREEN, 1.0, (255.0 - 15.0 * (float)i) / 255.0)); //255 - 15 * i));
  }

  for (int i = 1; i < 15 && (s - i + 60) % 60 != h && (s - i + 60) % 60 != m; i++) {
    Serial.printf("sec brightness<%f>\n", (255.0 - 15.0 * (float)i) / 255.0);
    //    strip.SetPixelColor((s - i + 60) % 60, HsbColor(HUE_BLUE, 1.0, (255.0 - 15.0 * (float)i)/255.0));
    strip.SetPixelColor((s - i + 60) % 60, RgbColor(0, 0, 255 - (15 * i)));
  }

  strip.SetPixelColor(h, RgbColor(100, 0, 0));
  strip.SetPixelColor(m, RgbColor(0, 50, 0));
  strip.SetPixelColor(s, RgbColor(0, 0, 25));

  strip.Show();
}

void modeSpectrum(int h, int m, int s, int tint_a, int tint_b) {
  strip.ClearTo(RgbColor(0, 0, 0));

  int count = (h - m + 60) % 60;
  RgbColor rgb_h = RgbColor(255, tint_a, tint_b);
  RgbColor rgb_m = RgbColor(tint_b, 255, tint_a);
  RgbColor rgb_s = RgbColor(tint_a, tint_b, 255);
  int s_count = (h - s + 60) % 60;
  if (s_count < count) {
    for (int i = 0; i <= s_count; i++) {
      strip.SetPixelColor((h - i + 60) % 60, RgbColor::LinearBlend(rgb_h, rgb_m, (float)i / (float)count)); // 255 * i / count)));
    }
    for (int i = s_count; i <= count; i++) {
      strip.SetPixelColor((h - i + 60) % 60,  RgbColor::LinearBlend(rgb_s, rgb_m, (float)(i - s_count) / (float)(count - s_count))); // 255 * (i - s_count) / (count - s_count)));
    }
  } else {
    for (int i = 0; i <= count; i++) {
      strip.SetPixelColor((h - i + 60) % 60, RgbColor::LinearBlend(rgb_h, rgb_m, (float)i / (float)count)); //255 * i / count));
    }
    strip.SetPixelColor(s, rgb_s);
  }
  strip.Show();
}

void modeHysterical(int h, int m, int s, int hueOffset) {
  Serial.printf("In modeHysterical. h<%d> m<%d> s<%d> hueOffset<>\n", h, m, s, hueOffset);
  strip.ClearTo(RgbColor(0, 0, 0));

  for (int i = 4; i < 60; i++) {
    for (int j = i - 4; j <= i; j++) {
      strip.SetPixelColor((s + j) % 60, HsbColor(HUE_BLUE/* + hueOffset*/, 1.0, 1.0));
    }
    //    strip.SetPixelColor(h, HsbColor((HUE_RED + hueOffset) % 255, 1.0, 1.0));
    strip.SetPixelColor(h, HsbColor(HUE_RED, 1.0, 1.0));
    //    strip.SetPixelColor(m, HsbColor(HUE_GREEN + hueOffset, 1.0, 255));
    strip.SetPixelColor(m, HsbColor(HUE_GREEN, 1.0, 1.0));
    for (int j = 0; j < 60; j++) {
      if (random(10) == 1 && j != h && j != m ) {
        strip.SetPixelColor(j, fadeToBlackBy(strip.GetPixelColor(j), 100));
      }
    }
    strip.Show();
    delay(1);
  }

  for (int i = 0; i < 30; i++) {
    //    strip.SetPixelColor(h, HsbColor((HUE_RED + hueOffset) % 255, 1.0, 1.0));
    //    strip.SetPixelColor(m, HsbColor(HUE_GREEN + hueOffset, 1.0, 1.0));
    //    strip.SetPixelColor(s, HsbColor(HUE_BLUE + hueOffset, 1.0, 1.0));
    strip.SetPixelColor(h, HsbColor(HUE_RED, 1.0, 1.0));
    strip.SetPixelColor(m, HsbColor(HUE_GREEN, 1.0, 1.0));
    strip.SetPixelColor(s, HsbColor(HUE_BLUE, 1.0, 1.0));
    for (int j = 0; j < 60; j++) {
      if (j != h && j != m && j != s) {
        strip.SetPixelColor(j, fadeToBlackBy(strip.GetPixelColor(j), 40));
      }
    }
    strip.Show();
    delay(3);
  }

  for (int i = 0; i < 60; i++) {
    if (i != h && i != m && i != s) {
      strip.SetPixelColor(i, 0); //RgbColor(0, 0, 0));
    }
  }

  strip.Show();
}

void sendHeader() {
  String html = "<!DOCTYPE HTML>";
  html += "<html><head><title>O Clock</title><link rel='icon' href='data:;base64,='>";
  html += "<meta http-equiv='Content-Type' content='text/html; charset=UTF-8' />";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<link rel='stylesheet' href='https://www.w3schools.com/w3css/4/w3.css'>";
  html += "<link rel='stylesheet' href='https://www.w3schools.com/lib/w3-theme-blue-grey.css'>";
  html += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.8.1/css/all.min.css'>";
  html += "</head><body>";
  server.sendContent(html);

  html = "</nav>";
  html += "<header class='w3-top w3-bar w3-theme'><h2 class='w3-bar-item'>O Clock</h2></header>";
  html += "<br><div class='w3-container w3-large' style='margin-top:88px'>";
  server.sendContent(html);
}

void sendFooter() {
  int8_t rssi = getWifiQuality();
  Serial.print("Signal Strength (RSSI): ");
  Serial.print(rssi);
  Serial.println("%");
  String html = "<br><br><br>";
  html += "</div>";
  html += "<footer class='w3-container w3-bottom w3-theme w3-margin-top'>";
  html += "<i class='far fa-paper-plane'></i> Version: " + String(VERSION) + "<br>";
  html += "<i class='fas fa-rss'></i> Signal Strength: ";
  html += String(rssi) + "%";
  html += "</footer>";
  html += "</body></html>";
  server.sendContent(html);
}

// converts the dBm to a range between 0 and 100%
int8_t getWifiQuality() {
  int32_t dbm = WiFi.RSSI();
  if (dbm <= -100) {
    return 0;
  } else if (dbm >= -50) {
    return 100;
  } else {
    return 2 * (dbm + 100);
  }
}

void onConfigure() {
  Serial.println("In onConfigure");
  int newmode = atoi(server.arg("displaymode").c_str());
  Serial.printf("Changing mode from <%d> to <%d>\n", displayMode, newmode);
  displayMode = newmode;

  redirectHome();
}

void displayHomePage() {
  Serial.println("In displayHomePage");
  String html = "";

  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  sendHeader();
  Serial.println("displayHomePage: header sent");

  html += "<form class='w3-container' action='/configure' method='get'><h2>Configure:</h2>";
  html += "<p>Select an operation mode:</p>";

  html += "<div><input type='radio' id='simple' name='displaymode' value='" + String(MODE_SIMPLE) + "'>";
  html += "<label for='simple'>Simple</label>";
  html += "</div>";

  html += "<div><input type='radio' id='minimalistic' name='displaymode' value='" + String(MODE_MINIMALISTIC) + "'>";
  html += "<label for='minimalistic'>Minimalistic</label>";
  html += "</div>";

  html += "<div><input type='radio' id='progress' name='displaymode' value='" + String(MODE_PROGRESS) + "'>";
  html += "<label for='progress'>Progress</label>";
  html += "</div>";

  html += "<div><input type='radio' id='sweep' name='displaymode' value='" + String(MODE_SWEEP) + "'>";
  html += "<label for='sweep'>Sweep</label>";
  html += "</div>";

  html += "<div><input type='radio' id='spectrum' name='displaymode' value='" + String(MODE_SPECTRUM) + "'>";
  html += "<label for='spectrum'>Spectrum</label>";
  html += "</div>";

  html += "<div><input type='radio' id='hysterical' name='displaymode' value='" + String(MODE_HYSTERICAL) + "'>";
  html += "<label for='hysterical'>Hysterical</label>";
  html += "</div>";
  html += "<p><button class='w3-button w3-block w3-green w3-section w3-padding' type='submit'>Save</button></p>";
  html += "</form>";

  html.replace("value='" + String(displayMode) + "'>", "value='" + String(displayMode) + "' checked>");

  server.sendContent(String(html)); // spit out what we got
  Serial.println("displayHomePage: body sent");
  html = ""; // fresh start

  sendFooter();
  Serial.println("displayHomePage: footer sent");
  server.sendContent("");
  server.client().stop();
}

void redirectHome() {
  Serial.println("In redirectHome");
  // Send them back to the Root Directory
  server.sendHeader("Location", String("/"), true);
  server.sendHeader("Cache-Control", "no-cache, no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(302, "text/plain", "");
  server.client().stop();
  delay(1000);
}

void initWiFi() {
  Serial.println("Initilizing WiFi");
  byte count = 0;
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    // Stop if cannot connect
    if (count >= 60) {
      Serial.println("Could not connect to local WiFi.");
      return;
    }

    delay(500);
    Serial.print(".");
    count++;
  }
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());

  IPAddress ip = WiFi.localIP();
  //  Serial.println(ip[3]);
}

void initOTA() {
  Serial.println("Initilizing OTA");
  String hostname(OTA_HOSTNAME);
  hostname += String(ESP.getChipId(), HEX);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  if (OTA_Password != "") {
    ArduinoOTA.setPassword(((const char *)OTA_Password.c_str()));
  }
  ArduinoOTA.begin();
}

void initWebServer() {
  Serial.println("Initilizing web server");
  server.on("/", displayHomePage);
  server.on("/configure", onConfigure);
  server.onNotFound(redirectHome);
  serverUpdater.setup(&server, "/update", www_username, www_password);
  // Start the server
  server.begin();
  Serial.println("Server started");
  // Print the IP address
  String webAddress = "http://" + WiFi.localIP().toString() + ":" + String(WEBSERVER_PORT) + "/";
  Serial.println("Use this URL : " + webAddress);
}

void nscale8x3( uint8_t& r, uint8_t& g, uint8_t& b, fract8 scale) {
  uint16_t scale_fixed = scale + 1;
  r = (((uint16_t)r) * scale_fixed) >> 8;
  g = (((uint16_t)g) * scale_fixed) >> 8;
  b = (((uint16_t)b) * scale_fixed) >> 8;
}

RgbColor fadeToBlackBy (RgbColor c, uint8_t fadefactor ) {
  nscale8x3( c.R, c.G, c.B, 255 - fadefactor);
  return c;
}

void initTime() {
  Serial.println("Initializing Time");

  udpTime.begin(timePort);
  timeClient.updateInterval(ntpInterval);
  timeClient.begin();
}

unsigned long getTime(boolean getLocal) {
  unsigned long epoch = timeClient.epoch();
  if (getLocal) {
    long tzAdjust = timeZoneOffset;
    //    Serial.printf("epoch<%lu> tzAdjust<%ld>\n", epoch, tzAdjust);
    tzAdjust *= 3600L;
    epoch += tzAdjust;
    if (observeDST && isDST(epoch)) {
      epoch += SECS_PER_HOUR;  // add one hour
    }
  }
  return epoch;
}

/**
   Daylight Savings Time begins on the second Sunday in March and
   ends on the 1st Sunday in November.
*/
boolean isDST(time_t t) {
  int d = day(t);
  int m = month(t);
  int y = year(t);
  int dow = weekday(t);

  //January, february, and december are out.
  if (m < 3 || m > 11) {
    return false;
  }
  //April to October are in
  if (m > 3 && m < 11) {
    return true;
  }

  int previousSunday = d - (dow - 1); // day of week begins on sunday=1, we need sunday==0

  //In march, we are DST if our previous sunday was on or after the 8th.
  if (m == 3) {
    return previousSunday >= 8;
  }
  //In november we must be before the first sunday to be dst.
  //That means the previous sunday must be before the 1st.
  return previousSunday <= 0;
}
