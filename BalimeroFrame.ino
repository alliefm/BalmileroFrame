/**
Balmilero Frame
Alicia Paterson alicia@aliciapaterson.com
This program controls the lights and IoT connectivity & behaviour
for the art frame for the Balmilero family.
It defines a string of 50 NeoPixel LEDs and an 8x8 LED NeoMatrix
Change the bottom few items to adjust the behaviour of the LEDs in the frame
**/

// Behavior variables
// lapseRate controls how long it takes for the LEDs in the heart to fade
// They go up to full over four steps. A lapseRate of 60 seconds will mean the heart 
// drops one level every 60 seconds.
// The default rate is 24 hours 
int lapseRate = 21600; // in seconds, 86400 is one day, 21600 is six hours
// maxBrightness limits how bright the LEDs can get. Other brightness levels are based on this value
// 0 is off and 255 is VERY bright. Default is 120
int maxBrightness = 180;
// adafruitIOdelay serves to limit how quickly the heart can increase to full
// If set to 3600 seconds, then it can only increaseone level every hour 
int adafruitIOdelay = 30; // in seconds
// The following items sets the color of the heart
// Use https://rgbcolorpicker.com/565 to chose your own value (the number between 0 and 65535)
#define HEART_COLOR 63497

// Libraries and config file
#include "config.h"
#include <WiFiUdp.h>
#include <FastLED.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <Adafruit_GFX.h>
#include <AdafruitIO_WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_NeoMatrix.h>

// NeoPixel configuration
#define STAR_DATA_PIN 0
#define HEART_DATA_PIN 2
#define NUM_LEDS 50
// Button configuration
#define BUTTON_PIN 4
// Twinkle effect settings
#define TWINKLE_PROBABILITY 5  // % chance of twinkle per LED per loop
#define TWINKLE_DIM_FACTOR  2  // Factor to dim twinkling LEDs

// NeoPixel setup
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, STAR_DATA_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(8, 8, HEART_DATA_PIN,
  NEO_MATRIX_TOP     + NEO_MATRIX_RIGHT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE,
  NEO_GRB            + NEO_KHZ800);

// Variables for modes and settings
int currentMode = 1;
unsigned long lastUpdated = 0;
unsigned long lastIOupdate = 0;
int minBrightness = 10;
int starFirstLED = 0;
int starLastLED = 49;
int buttonWait = 3600; // delay between button press readings
unsigned long buttonMillis = 0;

// Rainbow cycle variables
unsigned long previousMillis = 0;
const uint8_t rainbowWait = 100; // Time between updates in ms
uint16_t rainbowCycleIndex = 0;
uint16_t rainbowWheelIndex = 0;

//Twinkle variables
const uint8_t twinkleWait = 10; // Time between updates in ms
uint16_t twinkleIndex = 0;

//Heartbeat variables
const uint8_t heartWait = 10; // Time between updates in ms
uint16_t heartIndex = 0;
uint16_t heartColorIndex = minBrightness;
bool heartFilling = true;
unsigned long heartMillis = 0;

// Adafruit IO feeds
AdafruitIO_Feed *actionFlag = io.feed("balmileroframe.actionflag"); //balmileroframe.actionflag
AdafruitIO_Feed *lastUpdate = io.feed("balmileroframe.lastupdate");
AdafruitIO_Feed *buttonAction = io.feed("balmileroframe.buttonaction");
AdafruitIO_Feed *buttonLastUpdated = io.feed("balmileroframe.buttonlastupdated");
AdafruitIO_Feed *currentCloudMode = io.feed("balmileroframe.currentmode");

//Network Time Protocol setup
WiFiUDP ntpUDP;
//NTPClient timeClient(ntpUDP, "us.pool.ntp.org", -28800, 3600);
NTPClient timeClient(ntpUDP, "us.pool.ntp.org",0 ,0 );

// Twinkle effect
// I usually put functions after the main loop function, 
// however that seems to throw errors when I do it with this function,
// so it is here before the setup function
void twinkle(int start, int end, int r, int g, int b, int r2, int g2, int b2) {

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= twinkleWait) {
    previousMillis = currentMillis;
    if (twinkleIndex > end) {
      twinkleIndex = start;
    }

    int randColor = random(0, 2);
    if (randColor == 0) {
      strip.setPixelColor(twinkleIndex, strip.Color(r, g, b));
    } else {
      strip.setPixelColor(twinkleIndex, strip.Color(r2, g2, b2));
    }
    strip.show();
    twinkleIndex++;
  }
}

// Function to generate a random color
uint32_t getRandomColor() {
  return strip.Color(random(0, 256), random(0, 256), random(0, 256)); 
}

void setup() {
  Serial.begin(115200);

  timeClient.begin();
  io.connect();

  // wait for a connection
  while ((io.status() < AIO_CONNECTED))
  {
    Serial.print(".");
    delay(500);
  }
  Serial.println("Connected to Adafruit IO.");
  
  // Set up NeoPixel LEDs
  strip.begin();
  strip.show();

  // Set a random seed for lighting effects
  randomSeed(analogRead(0)); // Seed randomness

  // Set up NeoPixel Matrix
  matrix.begin();
  matrix.setBrightness(maxBrightness);
  matrix.clear();

  // Configure button input
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Set up a message handler for the actionFlag feed
  // to be called whenever a message is
  // received from adafruit io.
  actionFlag->onMessage(changeMode);
  currentCloudMode->onMessage(getMode);
  currentCloudMode->get();
  timeClient.update(); // Update time from NTP server
  lastUpdated = getEpochTime();

}

void loop() {

  //Check Adafruit IO regularly to see if new actions are present, frequency defined by adafruitIOdelay
  if (getEpochTime() > lastIOupdate + adafruitIOdelay) {
    io.run(); // Handle Adafruit IO
    lastIOupdate = getEpochTime();
  }

  // Check for button press
  if (digitalRead(BUTTON_PIN) == LOW) {
    handleButtonPress();
    delay(500); // Debounce
  }

  // Handle mode timeout
  if (getEpochTime() > lastUpdated + lapseRate) {
    if (currentMode > 1) {
      currentMode--;
    }
    lastUpdated = getEpochTime();
    Serial.print("Mode: ");
    Serial.println(currentMode);
    strip.clear();
    //Piggyback on this lapse rate timing to update NTP time from server
    timeClient.update(); // Update time from NTP server
  }

  // Handle display modes
  switch (currentMode) {
    case 0: {
      strip.clear();
      strip.show();
      matrix.clear();
      matrix.show();
    }
    break;

    case 1: {
      rainbowCycle(starFirstLED, starLastLED, maxBrightness);
      matrix.clear();
      matrix.show();
    }
    break;

    case 2: {
       rainbowCycle(starFirstLED, starLastLED, maxBrightness);
       solid(0,1);
    }
    break;

    case 3: {
      rainbowCycle(starFirstLED, starLastLED, maxBrightness);
      solid(0,3);
    }
    break;

    case 4: {
      rainbowCycle(starFirstLED, starLastLED, maxBrightness);
      solid(0,5);
    }
    break;

    case 5: {
      rainbowCycle(starFirstLED, starLastLED, maxBrightness);
      //twinkle(0, 49, 70, 70, 70, maxBrightness, maxBrightness, maxBrightness);
      solid(0,7);
    }
    break;
  }
  
}

// Handle button press
void handleButtonPress() {
  // Only action a button press if longer than delay defined at buttonWait since it was last pressed
  unsigned long currentMillis = millis();
  if (currentMillis - buttonMillis >= buttonWait) {
    buttonMillis = currentMillis;
    buttonAction->save(1);
    buttonLastUpdated->save(getCurrentDateTime());
    Serial.print("Button Pressed At: ");
    Serial.println(getCurrentDateTime());
  }
}

void getMode(AdafruitIO_Data *data) {
  String modeState = data->value();
  currentMode = modeState.toInt();
  Serial.print("Got Mode: ");
  Serial.println(currentMode);
}

// Respond to Action Flag events
void changeMode(AdafruitIO_Data *data) {
  // Check Adafruit IO actionFlag
  String flag = data->value();
  if (flag == "1") {
    lastUpdated = getEpochTime();
    if (currentMode < 5) currentMode++;
    currentCloudMode->save(currentMode);
    lastUpdate->save(getCurrentDateTime());
    Serial.print("Mode: ");
    Serial.println(currentMode);
  }
  // Special case: if mode is set to zero, turn the LEDs off
  // This can ONLY be set from https://aliciapaterson.com/frameoff.html
  if (flag == "0") {
    lastUpdated = getEpochTime();
    currentMode = 0;
    currentCloudMode->save(currentMode);
    lastUpdate->save(getCurrentDateTime());
    Serial.print("Mode: ");
    Serial.println(currentMode);
  }
  strip.clear();
}

// Get the current datetime (stub function)
String getCurrentDateTime() {
  time_t rawTime = getEpochTime();
  struct tm *timeInfo = gmtime(&rawTime);

  char buffer[30];
  sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02dZ",
          timeInfo->tm_year + 1900,
          timeInfo->tm_mon + 1,
          timeInfo->tm_mday,
          timeInfo->tm_hour,
          timeInfo->tm_min,
          timeInfo->tm_sec);
  return String(buffer);
}

// Get the current epoch time
unsigned long getEpochTime() {
  return timeClient.getEpochTime();
}

// Solid color function
void solid(int start, int rows) {
  matrix.clear();
  for (int i = start; i < rows; i+=2) {
    //draw two lines each time
    matrix.drawLine(i,0,i,7,HEART_COLOR);
    matrix.drawLine(i+1,0,i+1,7,HEART_COLOR);
  }
  matrix.show();
}

// Slightly different, this makes the rainbow equally distributed throughout
void rainbowCycle(int start, int end, int brightness) {

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= rainbowWait) {
    previousMillis = currentMillis;
    if (rainbowWheelIndex >= end) {
      rainbowWheelIndex = start;
    }
    strip.setPixelColor(rainbowWheelIndex, Wheel(((rainbowWheelIndex * 256 / end) + rainbowCycleIndex) & 255));
    rainbowWheelIndex++;
    strip.show();
    rainbowCycleIndex = (rainbowCycleIndex + 1) % 256; // Loop back to 0 after a full cycle
  }
}

// Generate rainbow color
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  } else {
    WheelPos -= 170;
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  }
}
