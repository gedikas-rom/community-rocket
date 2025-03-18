#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <VL53L0X.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Credentials.h>
#include <Preferences.h>
#include <MQTT_ha.h>

// WiFi Einstellungen
const char* hostname = "rocket";

const char* firmware = "0.9.0";

// MQTT Einstellungen
const char* mqtt_server = "192.168.179.23"; //"iobroker.fritz.box";
const int mqtt_port = 1890;
const char* mqtt_user = "";      // Optional
const char* mqtt_password = "";  // Optional

const char* mqtt_topic_watersum = "rocket/wasserstand";
const char* mqtt_topic_water = "rocket/wasserstand/fuellstand";
const char* mqtt_topic_distance = "rocket/wasserstand/distanz";
const char* mqtt_topic_status = "rocket/wasserstand/status";
const char* mqtt_topic_refills = "rocket/wasserstand/auffuellungen";
const char* mqtt_topic_command = "rocket/wasserstand/command";  // Eingehende Befehle
const char* mqtt_topic_firmware = "rocket/wasserstand/firmware";  // aktuelle Firmware Version

// Einstellungen
Preferences preferences;
const char* prefFile = "rocket";
const char* prefValueRefills = "refills";

// Schwellwerte
const float WATER_LEVEL_THRESHOLD = 10.0;     // Mindeständerung für MQTT Update in %
const float REFILL_THRESHOLD = 50.0;         // Mindestanstieg für Auffüllerkennung in %
const int REFILL_TIME_WINDOW = 10000;        // Zeitfenster für Auffüllerkennung in ms

// Konfiguration für LED Ring
#define NUM_LEDS 16
#define LED_PIN 1
#define COLOR_ORDER GRB
#define LED_TYPE WS2812B
#define BRIGHTNESS 128
// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Definition der Wasserstands-Grenzen in mm
#define WATER_FULL 45
#define WATER_EMPTY 250
#define SENSOR_OFFSET 0 //-35 // Offset in mm 

VL53L0X sensor;
WiFiClient espClient;
PubSubClient mqtt(espClient);

// Globale Variablen für den letzten gemessenen Wasserstand
float lastPublishedWaterLevel = -1;
float lastWaterLevel = -1;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastWaterLevelCheck = 0;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000; // 5 Sekunden zwischen Reconnect-Versuchen

// Zähler für Auffüllvorgänge
uint32_t refillCount = 0;
bool isRefilling = false;

// JSON Buffer für MQTT Nachrichten
JsonDocument jsonDoc;

void updateLEDRing(float waterLevel);
void colorProgress(uint32_t color, int progress, int total);
void blink(uint32_t color, int wait);
void colorWipe(uint32_t color, int wait);
void colorFill(uint32_t color);
void publishRefillCount();
void publishJSONDoc();

// MQTT Callback für eingehende Nachrichten
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Payload in String umwandeln
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  String command = String(message);
  
  if (String(topic) == mqtt_topic_command) {
    if (command == "reset_refill_counter") {
      refillCount = 0;
      publishRefillCount();
      mqtt.publish(mqtt_topic_command, " ", true); // Command zurücksetzen nach der Verarbeitung
      Serial.println("Auffüllzähler zurückgesetzt");
    }
  } 
}

void publishJSONDoc() {
    // JSON Objekt für strukturierte Daten
    char jsonBuffer[200];
    serializeJson(jsonDoc, jsonBuffer);
    mqtt.publish(mqtt_topic_watersum, jsonBuffer, true);
}

void publishRefillCount() {
  char refillStr[10];
  itoa(refillCount, refillStr, 10);
  mqtt.publish(mqtt_topic_refills, refillStr, true);
  jsonDoc["auffuellungen"] = refillCount;
  publishJSONDoc();
  preferences.begin(prefFile, false);
  preferences.putUInt(prefValueRefills, refillCount);
  preferences.end();
}

bool connectMQTT() {
  if (mqtt.connected()) {
    return true;
  }

  Serial.print("Verbinde mit MQTT Broker...");
  
  // Client ID generieren
  String clientId = "ROCKET-ESP32";
  
  // Verbindungsversuch mit Credentials
  bool connected = false;
  if (mqtt_user && mqtt_password) {
    connected = mqtt.connect(clientId.c_str(), mqtt_user, mqtt_password, 
                           mqtt_topic_status, 1, true, "offline");
  } else {
    connected = mqtt.connect(clientId.c_str(), mqtt_topic_status, 1, true, "offline");
  }

  if (connected) {
    Serial.println("verbunden");
    // Online Status publizieren
    mqtt.publish(mqtt_topic_status, "online", true);
    mqtt.publish(mqtt_topic_firmware, firmware, true);
    jsonDoc["firmware"] = firmware;
    mqtt.subscribe(mqtt_topic_command);
    
    // Setting Homeassistant sensor config
    Serial.println("--> HA Config");
    mqtt.setBufferSize(800);
    Serial.println(mqtt.publish(mqtt_topic_ha_auffuellungen.c_str(), mqtt_ha_config_auffuellungen, true));
    Serial.println(mqtt.publish(mqtt_topic_ha_distanz.c_str(), mqtt_ha_config_distanz, true));
    Serial.println(mqtt.publish(mqtt_topic_ha_fuellstand.c_str(), mqtt_ha_config_fuellstand, true));
    Serial.println(mqtt.publish(mqtt_topic_ha_command.c_str(), mqtt_ha_config_command, true));
    Serial.println(mqtt.publish(mqtt_topic_ha_firmware.c_str(), mqtt_ha_config_firmware, true));

    // Gespeicherte Werte lesen, bspw. nach Neustart
    if (refillCount == 0)
    {
      preferences.begin(prefFile, false);
      refillCount = preferences.getUInt(prefValueRefills, 0);
      preferences.end();
    }  

    publishRefillCount(); // Aktuellen Zählerstand senden
    return true;
  } else {
    Serial.print("fehlgeschlagen, rc=");
    Serial.println(mqtt.state());
    return false;
  }
}

void checkForRefill(float currentWaterLevel) {
  unsigned long now = millis();
  
  // Erste Messung
  if (lastWaterLevel < 0) {
    lastWaterLevel = currentWaterLevel;
    lastWaterLevelCheck = now;
    return;
  }
  
  // Prüfen ob signifikanter Anstieg im Zeitfenster
  if (now - lastWaterLevelCheck <= REFILL_TIME_WINDOW) {
    float waterLevelChange = currentWaterLevel - lastWaterLevel;
    
    // Wenn Wasserstand deutlich gestiegen ist und wir noch nicht im Auffüllmodus sind
    if (waterLevelChange >= REFILL_THRESHOLD && !isRefilling) {
      isRefilling = true;
      refillCount++;
      publishRefillCount();
      
      // Visuelle Bestätigung auf LED Ring
      for(int i = 0; i < 3; i++) {
        blink(strip.Color(0,0,255), 100);
        }
    }
  } else {
    // Zeitfenster abgelaufen, Reset für neue Erkennung
    isRefilling = false;
    lastWaterLevel = currentWaterLevel;
    lastWaterLevelCheck = now;
  }
}

void publishWaterLevel(float waterLevel, int distance) {
  if (!mqtt.connected()) {
    return;
  }

  // Nur publizieren wenn die Änderung größer als der Schwellwert ist
  if (abs(waterLevel - lastPublishedWaterLevel) >= WATER_LEVEL_THRESHOLD || 
      lastPublishedWaterLevel < 0) {
    
    // Einzelne Werte für einfache Verarbeitung
    char waterLevelStr[10];
    dtostrf(waterLevel, 1, 1, waterLevelStr);
    mqtt.publish(mqtt_topic_water, waterLevelStr, true);
    
    char distanceStr[10];
    itoa(distance, distanceStr, 10);
    mqtt.publish(mqtt_topic_distance, distanceStr, true);
    
    lastPublishedWaterLevel = waterLevel;
    
    Serial.print("MQTT Update - Füllstand: ");
    Serial.print(waterLevelStr);
    Serial.print("%, Distanz: ");
    Serial.print(distanceStr);
    Serial.print("mm, Auffüllungen: ");
    Serial.println(refillCount);

    jsonDoc["fuellstand"] = waterLevel;
    jsonDoc["distanz"] = distance;
    publishJSONDoc();
  }
}

void setupMQTT() {
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);
  connectMQTT();
}

// LED Statusanzeige für OTA
void showOTAProgress(unsigned int progress, unsigned int total) {
  colorProgress(strip.Color(0,0,255), progress, total);
}

void setupOTA() {
  ArduinoOTA.setHostname(hostname);
  
  ArduinoOTA
    .onStart([]() {
      colorFill(strip.Color(158, 37, 190));
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      showOTAProgress(progress, total);
    })
    .onEnd([]() {
      colorFill(strip.Color(0,255,0));
      delay(500);
    })
    .onError([](ota_error_t error) {
      colorFill(strip.Color(255,0,0));
    });

  ArduinoOTA.begin();
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);
  WiFi.begin(WIFI_ssid, WIFI_password);
  
  while (WiFi.status() != WL_CONNECTED) {
    colorFill(strip.Color(0,0,0));
    colorWipe(strip.Color(255, 255, 0), 20);
  }
  
  colorFill(strip.Color(0, 255, 0));
  delay(500);
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  // LED Ring initialisieren
  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(50); // Set BRIGHTNESS to about 1/5 (max = 255)

  // WiFi, OTA und MQTT Setup
  setupWiFi();
  setupOTA();
  setupMQTT();
  
  // ToF Sensor initialisieren
  sensor.init();
  sensor.setTimeout(500);
  sensor.startContinuous(1000);
}

void loop() {
  // OTA Update Handler
  ArduinoOTA.handle();
  
  // MQTT Verbindung prüfen und ggf. wiederherstellen
  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastMqttReconnectAttempt > MQTT_RECONNECT_INTERVAL) {
      lastMqttReconnectAttempt = now;
      if (connectMQTT()) {
        lastMqttReconnectAttempt = 0;
      }
    }
  }
  mqtt.loop();
  
  // Wasserhöhe messen
  uint16_t distance = sensor.readRangeContinuousMillimeters();
  distance = distance + SENSOR_OFFSET;

  if (sensor.timeoutOccurred()) {
    Serial.println("Sensor timeout!");
    return;
  }
  
  // Wasserhöhe in Prozent umrechnen
  float waterLevel = map(constrain(distance, WATER_FULL, WATER_EMPTY),
                        WATER_FULL, WATER_EMPTY,
                        100, 0);

  // Prüfen ob gerade aufgefüllt wird
  checkForRefill(waterLevel);
            
  // MQTT Update
  publishWaterLevel(waterLevel, distance);
  
  // LED Ring aktualisieren
  updateLEDRing(waterLevel);
  
  delay(100);
}

void updateLEDRing(float waterLevel) {
  //waterLevel = 90.0;
  uint8_t green = 255;
  long red = map(waterLevel, 0, 100, 0, 510);
  
  // bei 100% = 510; red=0, green=255
  // 50% = 255; red=255, green=255
  // 0% = 0; red=255, green=0
  if (red > 255)
    {
      green = 255;
      red = 510-red;
    }
  else
  {
      green = red;
      red = 255; 
  }
  for(int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(red, green, 0));
  }
  strip.show(); 
}

// LED Helper functions
void colorProgress(uint32_t color, int progress, int total) {
  int progressLeds = map(progress, 0, total, 0, NUM_LEDS);
  for(int i = 0; i < NUM_LEDS; i++) {
    if(i < progressLeds) {
      strip.setPixelColor(i, color);
    } else {
      strip.setPixelColor(i, strip.Color(0,0,0));
    }
  }
  strip.show();
}

void blink(uint32_t color, int wait) {
  colorFill(color);
  delay(wait);                           //  Pause for a moment
  colorFill(strip.Color(0,0,0));
  delay(wait);                           //  Pause for a moment
}

void colorWipe(uint32_t color, int wait) {
  for(int i=0; i<strip.numPixels(); i++) { // For each pixel in strip...
    strip.setPixelColor(i, color);         //  Set pixel's color (in RAM)
    strip.show();                          //  Update strip to match
    delay(wait);                           //  Pause for a moment
  }
}

void colorFill(uint32_t color) {
  for(int i=0; i<strip.numPixels(); i++) { // For each pixel in strip...
    strip.setPixelColor(i, color);         //  Set pixel's color (in RAM)
  }
  strip.show();                          //  Update strip to match
}