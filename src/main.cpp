/**
 * File: main.cpp
 * Started 27.10.20.20
 * Copyright 2020 Tauno Erik
 * 
 * Board: NodeMcu V1.0 ESP12E / Lolin ver 0.1
 *        D1 - SCL
 *        D2 - SDA
 * Sensors: - HTU21D-F Temperature & Humidity Sensor
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
#include "Tauno_secrets.h"

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

/* OLED */
const int SCREEN_WIDTH {128};  // OLED display width, in pixels
const int SCREEN_HEIGHT {32};  // OLED display height, in pixels
const int OLED_RESET_PIN {0};
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);

/* HTU21DF */
Adafruit_HTU21DF Htu = Adafruit_HTU21DF();

/* CCS811 */
Adafruit_CCS811 Ccs;

/* millis() */
uint32_t previous_millis {};
const uint MEASURE_INTERVAL {60*1000};  // 60s
bool is_measurement_time = false;

WiFiClient  client;

/********************************************/

void maintain_wifi() {
  /* Maintain WiFi connection */
  if (wifiMulti.run(connectTimeoutMs) == WL_CONNECTED) {
    DEBUG_PRINT("WiFi SSID: ");
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
}

void loop() {
  uint32 current_millis = millis();
  if ((current_millis - previous_millis) >= MEASURE_INTERVAL) {
    is_measurement_time = true;
    previous_millis = current_millis;
  }

  if (is_measurement_time) {
    is_measurement_time = false;
    maintain_wifi();

    /* HTU21D humidity sensor */
    float htu21_temp = Htu.readTemperature();
    float htu21_hum = Htu.readHumidity();
    DEBUG_PRINT("Temp: ");
    DEBUG_PRINT(htu21_temp);
    DEBUG_PRINT(" Hum: ");
    DEBUG_PRINTLN(htu21_hum);

    /* CCS811 co2 sensor */
    bool ccs811_available = Ccs.available();  // True if data is available
    int ccs811_eCO2 {};
    int ccs811_TVOC {};

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

    /* ThingSpeak */
    // Prepare ThingSpeak package
    ThingSpeak.setField(1, ccs811_eCO2);
    ThingSpeak.setField(2, ccs811_TVOC);
    ThingSpeak.setField(3, htu21_temp);
    ThingSpeak.setField(4, htu21_hum);
    // Send to ThingSpeak
    int http_status = ThingSpeak.writeFields(Secret::id, Secret::key);
    if (http_status != 200) {
      DEBUG_PRINT("Http error: ");
      DEBUG_PRINTLN(http_status);
    }
  }  // time end
}
