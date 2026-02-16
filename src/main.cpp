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
#include <WebServer.h>

// WiFi Einstellungen
const char* hostname = "rocket";

const char* firmware = "0.9.5";

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
const float REFILL_THRESHOLD = 30.0;         // Mindestanstieg für Auffüllerkennung in %
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
#define WATER_FULL 50
#define WATER_EMPTY 230
#define SENSOR_OFFSET 0 //-35 // Offset in mm 

VL53L0X sensor;
WiFiClient espClient;
PubSubClient mqtt(espClient);

// Webserver für Konfiguration
WebServer server(80);

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

// HTML für die Konfigurationsseite
const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE HTML>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Rocket WLAN Konfiguration</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 500px; margin: 0 auto; padding: 20px; }
        h1 { color: #333; text-align: center; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input[type="text"], input[type="password"] { width: 100%; padding: 8px; box-sizing: border-box; border: 1px solid #ddd; border-radius: 4px; }
        button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; width: 100%; }
        button:hover { background-color: #45a049; }
        .status { padding: 10px; margin: 10px 0; border-radius: 4px; }
        .success { background-color: #dff0d8; color: #3c763d; }
        .error { background-color: #f2dede; color: #a94442; }
        .info { background-color: #d9edf7; color: #31708f; }
    </style>
</head>
<body>
    <h1>Rocket WLAN Konfiguration</h1>
    <div class="status info">
        Verbinden Sie sich mit dem WLAN "Rocket-Config" um diese Seite zu erreichen.<br>
        Aktuelle IP: %s
    </div>
    <form action="/save" method="POST">
        <div class="form-group">
            <label for="ssid">WLAN SSID:</label>
            <input type="text" id="ssid" name="ssid" required>
        </div>
        <div class="form-group">
            <label for="password">WLAN Passwort:</label>
            <input type="password" id="password" name="password">
        </div>
        <button type="submit">Speichern und neu starten</button>
    </form>
    <div id="message" class="status" style="display: none;"></div>
    <script>
        // Zeige Statusmeldung wenn vorhanden
        const urlParams = new URLSearchParams(window.location.search);
        const message = urlParams.get('message');
        const status = urlParams.get('status');
        if (message) {
            document.getElementById('message').style.display = 'block';
            document.getElementById('message').innerHTML = message;
            document.getElementById('message').className = 'status ' + status;
        }
    </script>
</body>
</html>
)=====";

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

// Webserver Handler
void handleRoot() {
  char html[sizeof(INDEX_HTML) + 50];
  sprintf(html, INDEX_HTML, WiFi.softAPIP().toString().c_str());
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    
    // Speichere die Anmeldeinformationen in den Preferences
    preferences.begin(prefFile, false);
    preferences.putString("wifi_ssid", ssid);
    preferences.putString("wifi_password", password);
    preferences.end();
    
    // Sende Erfolgmeldung
    String redirectUrl = "/?message=Einstellungen+gespeichert.+Das+Ger%C3%A4t+startet+neu...&status=success";
    server.sendHeader("Location", redirectUrl);
    server.send(303);
    
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Fehler: SSID und Passwort erforderlich");
  }
}

void handleNotFound() {
  String message = "Datei nicht gefunden\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP-Server gestartet");
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
  // Versuche, gespeicherte Anmeldeinformationen zu laden
  preferences.begin(prefFile, true); // Read-only Modus
  String savedSsid = preferences.getString("wifi_ssid", "");
  String savedPassword = preferences.getString("wifi_password", "");
  preferences.end();
  
  const char* ssidToUse = savedSsid.length() > 0 ? savedSsid.c_str() : WIFI_ssid;
  const char* passwordToUse = savedPassword.length() > 0 ? savedPassword.c_str() : WIFI_password;
  
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);
  WiFi.begin(ssidToUse, passwordToUse);
  
  // Versuche, eine Verbindung zum konfigurierten WLAN herzustellen
  int attempt = 0;
  const int maxAttempts = 10;
  
  while (WiFi.status() != WL_CONNECTED && attempt < maxAttempts) {
    colorFill(strip.Color(0,0,0));
    colorWipe(strip.Color(255, 255, 0), 20);
    attempt++;
    delay(500);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    // Erfolgreich mit dem WLAN verbunden
    colorFill(strip.Color(0, 255, 0));
    delay(500);
    Serial.println("Verbunden mit WLAN: " + String(ssidToUse));
    Serial.println("IP-Adresse: " + WiFi.localIP().toString());
  } else {
    // Verbindung fehlgeschlagen, starte Access Point als Fallback
    Serial.println("Verbindung zu WLAN fehlgeschlagen, starte Access Point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Rocket-Config", "rocket123");
    
    // LED-Anzeige für Access Point Modus
    colorFill(strip.Color(0, 0, 255));
    delay(500);
    colorFill(strip.Color(0, 0, 0));
    delay(500);
    colorFill(strip.Color(0, 0, 255));
    
    // Starte den Webserver für die Konfiguration
    setupWebServer();
    
    Serial.println("Access Point gestartet");
    Serial.println("SSID: Rocket-Config");
    Serial.println("Passwort: rocket123");
    Serial.println("IP-Adresse: " + WiFi.softAPIP().toString());
    Serial.println("Öffnen Sie einen Browser und gehen Sie zu: http://" + WiFi.softAPIP().toString());
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  // LED Ring initialisieren
  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(50); // Set BRIGHTNESS to about 1/5 (max = 255)

  // WiFi Setup
  setupWiFi();
  
  // ToF Sensor initialisieren
  sensor.init();
  sensor.setTimeout(500);
  sensor.startContinuous(1000);
  
  // OTA und MQTT nur starten, wenn wir mit einem WLAN verbunden sind
  if (WiFi.status() == WL_CONNECTED) {
    setupOTA();
    setupMQTT();
  }
}

void loop() {
  // OTA Update Handler (nur wenn mit WLAN verbunden)
  if (WiFi.status() == WL_CONNECTED) {
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
  } else {
    // Webserver bedienen, wenn wir im Access Point Modus sind
    server.handleClient();
  }
  
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
            
  // MQTT Update (nur wenn mit WLAN verbunden)
  if (WiFi.status() == WL_CONNECTED) {
    publishWaterLevel(waterLevel, distance);
  }
  
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