/**
 * File: main.cpp
 * Started 27.10.20.20
 * Copyright 2020 Tauno Erik
 * 
 * Board: NodeMcu V1.0 ESP12E / Lolin ver 0.1
 *        D1 - SCL
 *        D2 - SDA
 * Sensors: - HTU21D-F Temperature & Humidity Sensor
 * 
 * TODO: 
 *  - ledi kiirus olenevalt liikumisest
 *  - ledi värvi muutumine ajajooksul
 */
#include <Arduino.h>
#include <ESP8266WiFiMulti.h>
#include <SPI.h>                  // OLED
#include <Wire.h>                 // I2C
#include <Adafruit_GFX.h>         // OLED
#include <Adafruit_SSD1306.h>     // OLED
#include <Fonts/FreeSans9pt7b.h>  // OLED
#include <Adafruit_HTU21DF.h>     // HTU21
#include <Adafruit_CCS811.h>      // CCS811
#include <ThingSpeak.h>
#include <Adafruit_NeoPixel.h>
#include "Tauno_secrets.h"        // Passwords

/* Enable debug info Serial print */
#define DEBUG
#ifdef DEBUG
  #define DEBUG_PRINT(x)  Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

/* Wifi */
ESP8266WiFiMulti wifiMulti;
// WiFi connect timeout per AP. Increase when connecting takes longer.
const uint32_t connectTimeoutMs {5000};

WiFiClient  client;  // ThingSpeak

/* OLED display */
const int SCREEN_WIDTH {128};  // OLED display width, in pixels
const int SCREEN_HEIGHT {32};  // OLED display height, in pixels
const int OLED_RESET_PIN {0};
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);
int display_sequence {};  // data display order on OLED
bool is_inverted {true};

/* HTU21DF */
Adafruit_HTU21DF Htu = Adafruit_HTU21DF();
float htu21_temp {};
float htu21_hum {};

/* CCS811 */
Adafruit_CCS811 Ccs;
int ccs811_eCO2 {};
int ccs811_TVOC {};

/* RCWK-0516 */
const int RADAR_PIN {14};  // D5
int radar_data {};

/* NeoPixel stripe */
const int PIXEL_PIN {12};  // D6
const int PIXEL_COUNT {8};
Adafruit_NeoPixel pixel(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
// Argument 1 = Number of pixels in NeoPixel strip
// Argument 2 = Arduino pin number (most are valid)
// Argument 3 = Pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)

/* millis() */
uint32_t prev_measure_ms {};
const uint MEASURE_INTERVAL {60*1000};  // 60s
bool is_measurement_time = false;

uint32_t prev_display_ms {};
const uint DISPLAY_CHANGE_INTERVAL {2*1000};  // 2s
bool is_display_change_time = false;

uint32_t prev_pixel_ms {};
const uint PIXEL_INTERVAL {300};
const uint SLOW {300};
const uint FAST {100};
bool is_pixel_time = false;
int current_pixel {};
uint pixel_speed {SLOW};
uint8_t r {};
uint8_t g {255};
uint8_t b {};

/********************************************/
void pixel_rainbow(int wait) {
  // Hue of first pixel runs 5 complete loops through the color wheel.
  // Color wheel has a range of 65536 but it's OK if we roll over, so
  // just count from 0 to 5*65536. Adding 256 to firstPixelHue each time
  // means we'll make 5*65536/256 = 1280 passes through this outer loop:
  for (int32 firstPxHue = 0; firstPxHue < 5*65536; firstPxHue += 256) {
    for (int i = 0; i < pixel.numPixels(); i++) {  // For each pixel in strip...
      // Offset pixel hue by an amount to make one full revolution of the
      // color wheel (range of 65536) along the length of the strip
      // (strip.numPixels() steps):
      int pixelHue = firstPxHue + (i * 65536L / pixel.numPixels());
      // strip.ColorHSV() can take 1 or 3 arguments: a hue (0 to 65535) or
      // optionally add saturation and value (brightness) (each 0 to 255).
      // Here we're using just the single-argument hue variant. The result
      // is passed through strip.gamma32() to provide 'truer' colors
      // before assigning to each pixel:
      pixel.setPixelColor(i, pixel.gamma32(pixel.ColorHSV(pixelHue)));
    }
    pixel.show();  // Update strip with new contents
    delay(wait);  // Pause for a moment
  }
}

void pixel_off() {
  pixel.clear();
  pixel.show();
}

/********************************************/

void maintain_wifi() {
  /* Maintain WiFi connection */
  if (wifiMulti.run(connectTimeoutMs) == WL_CONNECTED) {
    DEBUG_PRINT("\nWiFi SSID: ");
    DEBUG_PRINT(WiFi.SSID());
    DEBUG_PRINT(" ");
    DEBUG_PRINTLN(WiFi.localIP());
  } else {
    DEBUG_PRINTLN("WiFi not connected!");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Tsentri ENV monitor: eCO2, temp and hum.");
  Serial.print("Compiled: ");
  Serial.print(__TIME__);
  Serial.print(" ");
  Serial.println(__DATE__);
  Serial.println("Made by Tauno Erik.");

  /* Wifi setuo */
  // Set WiFi to station mode
  WiFi.mode(WIFI_STA);

  // Register multi WiFi networks
  // wifiMulti.addAP("ssid", "your_password");
  wifiMulti.addAP(Secret::ssd1, Secret::pass1);
  wifiMulti.addAP(Secret::ssd2, Secret::pass2);
  wifiMulti.addAP(Secret::ssd3, Secret::pass3);
  wifiMulti.addAP(Secret::ssd4, Secret::pass4);

  /* OLED setup */
  // Address 0x3C for 128x32
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    DEBUG_PRINTLN(F("Failed to start OLED SSD1306!"));
    // for(;;); // Don't proceed, loop forever
  }
  display.display();  // Adafruit splash screen
  delay(2000);
  display.clearDisplay();  // Clear the buffer
  display.setFont(&FreeSans9pt7b);
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.invertDisplay(is_inverted);

  /* HTU21D setup */
  if (!Htu.begin()) {
    DEBUG_PRINTLN(F("Failed to start HTU21D!"));
    // while(1) delay(1);
  }

  /* CCS811 setup */
  if (!Ccs.begin()) {
    DEBUG_PRINTLN(F("Failed to start CCS811!"));
    // while(1) delay(1);
  }

  /* ThingSpeak setup */
  ThingSpeak.begin(client);

  /* */
  pinMode(RADAR_PIN, INPUT);

  /* NeoPixel stripe*/
  pixel.begin();
  pixel.show();  // Turn OFF all pixels ASAP
  pixel.setBrightness(50);  // Set BRIGHTNESS to about 1/5 (max = 255)
}


void loop() {
  uint32_t current_ms = millis();

  // Check the time: display change
  if ((current_ms - prev_display_ms) >= DISPLAY_CHANGE_INTERVAL) {
    is_display_change_time = true;
    prev_display_ms = current_ms;
  }

  // Check the time: measurment
  if ((current_ms - prev_measure_ms) >= MEASURE_INTERVAL) {
    is_measurement_time = true;
    prev_measure_ms = current_ms;
  }

  // Check the time: pixel change
  if ((current_ms - prev_pixel_ms) >= pixel_speed) {
    is_pixel_time = true;
    prev_pixel_ms = current_ms;
  }

  // Motion detection is on all time!
  bool motion = digitalRead(RADAR_PIN);
  // If there was a movement, we store it.
  if (motion) {
    radar_data = 1;
    r = 200;
    g = 10;
    b = 10;
    pixel_speed = FAST;
  }

  if (is_pixel_time) {
    is_pixel_time = false;
    DEBUG_PRINT("Current px ");
    DEBUG_PRINT(current_pixel);
    DEBUG_PRINT(" r=");
    DEBUG_PRINT(r);
    DEBUG_PRINT(" g=");
    DEBUG_PRINT(g);
    DEBUG_PRINT(" b=");
    DEBUG_PRINTLN(b);
    if (current_pixel == PIXEL_COUNT) {
      current_pixel = 0;
    }
    pixel.clear();
    pixel.setPixelColor(current_pixel, r, g, b);  // (n, r, g, b)
    pixel.show();
    current_pixel++;
    r = 10;
    g = 200;
    b = 10;
    pixel_speed = SLOW;
  }

  if (is_measurement_time) {
    is_measurement_time = false;
    maintain_wifi();

    /* HTU21D humidity sensor */
    htu21_temp = Htu.readTemperature();
    htu21_hum = Htu.readHumidity();
    DEBUG_PRINT("Temp: ");
    DEBUG_PRINT(htu21_temp);
    DEBUG_PRINT(" Hum: ");
    DEBUG_PRINTLN(htu21_hum);

    /* CCS811 co2 sensor */
    bool ccs811_available = Ccs.available();  // True if data is available

    if (ccs811_available) {
      bool ccs811_error = Ccs.readData();  // True if an error
      if (!ccs811_error) {
        ccs811_eCO2 = Ccs.geteCO2();
        ccs811_TVOC = Ccs.getTVOC();
        DEBUG_PRINT("eCO2: ");
        DEBUG_PRINT(ccs811_eCO2);
        DEBUG_PRINT(" TVOC: ");
        DEBUG_PRINTLN(ccs811_TVOC);
      } else {
        DEBUG_PRINTLN("CCS811 reading error!");
      }
    }

    if (radar_data) {
      DEBUG_PRINTLN("There was a movement.");
    } else {
      DEBUG_PRINTLN("There was no movement.");
    }

    /* ThingSpeak */
    // Prepare ThingSpeak package
    ThingSpeak.setField(1, ccs811_eCO2);
    ThingSpeak.setField(2, ccs811_TVOC);
    ThingSpeak.setField(3, htu21_temp);
    ThingSpeak.setField(4, htu21_hum);
    ThingSpeak.setField(5, radar_data);
    // Send to ThingSpeak
    int http_status = ThingSpeak.writeFields(Secret::id, Secret::key);
    if (http_status != 200) {
      DEBUG_PRINT("Http error: ");
      DEBUG_PRINTLN(http_status);
    }
    radar_data = 0;  // After we have send data
  }  // is_measurement_time end

  /* OLED display: */
  if (is_display_change_time) {
    is_display_change_time = false;
    display_sequence += 1;
    display.setCursor(3, 25);  // oleneb fondist!
    // flip display
    is_inverted ^= 1;
    display.invertDisplay(is_inverted);

    // iga 1000ms järel kuvame uut näitu
    switch (display_sequence) {
    case 1:
      display.setTextSize(2);
      display.print(htu21_temp);
      display.setTextSize(1);
      display.print(" C");
      display.display();
      display.clearDisplay();
      break;
    case 2:
    display.setTextSize(2);
      display.print(htu21_hum);
      display.setTextSize(1);
      display.print(" %");
      display.display();
      display.clearDisplay();
      break;
    case 3:
      display.setTextSize(2);
      display.print(ccs811_eCO2);
      display.setTextSize(1);
      display.print(" ppm");
      display.display();
      display.clearDisplay();
      break;
    case 4:
      display.setTextSize(2);
      display.print(ccs811_TVOC);
      display.setTextSize(1);
      display.print(" ppb");
      display.display();
      display.clearDisplay();
      break;
    default:
      display_sequence = 0;
      break;
    }  // switch end
  }  // is_display_change_time end
}
