// Homeassistant sensor config
#include <string>

// Base topic
const char* mqtt_topic_ha_base = "hass";

// Concatenated topics
std::string mqtt_topic_ha_auffuellungen = std::string(mqtt_topic_ha_base) + "/sensor/kaffeemaschine/auffuellungen/config";
std::string mqtt_topic_ha_distanz = std::string(mqtt_topic_ha_base) + "/sensor/kaffeemaschine/distanz/config";
std::string mqtt_topic_ha_fuellstand = std::string(mqtt_topic_ha_base) + "/sensor/kaffeemaschine/fuellstand/config";
std::string mqtt_topic_ha_command = std::string(mqtt_topic_ha_base) + "/button/kaffeemaschine/resetauffuellungen/config";
std::string mqtt_topic_ha_firmware = std::string(mqtt_topic_ha_base) + "/sensor/kaffeemaschine/firmware/config";

const char* mqtt_ha_config_auffuellungen = R"rawliteral({
  "device": {
    "identifiers": [
      "kaffeemaschine"
    ],
    "manufacturer": "Rocket",
    "model": "Appartemento",
    "name": "Kaffeemaschine"
  },
  "enabled_by_default": true,
  "object_id": "kaffeemaschine_auffuellungen",
  "origin": {
    "name": "ESP32-C6",
    "sw": "1.0.0",
    "url": "https://wiki.seeedstudio.com/xiao_pin_multiplexing_esp33c6"
  },
  "name": "Auffüllungen",
  "icon": "mdi:counter",
  "state_class": "measurement",
  "state_topic": "rocket/wasserstand",
  "unique_id": "kaffeemaschine_auffuellungen",
  "unit_of_measurement": "",
  "value_template": "{{ value_json.auffuellungen }}"
})rawliteral";

const char* mqtt_ha_config_distanz = R"rawliteral({
  "device": {
    "identifiers": [
      "kaffeemaschine"
    ],
    "manufacturer": "Rocket",
    "model": "Appartemento",
    "name": "Kaffeemaschine"
  },
  "enabled_by_default": true,
  "object_id": "kaffeemaschine_distanz",
  "origin": {
    "name": "ESP32-C6",
    "sw": "1.0.0",
    "url": "https://wiki.seeedstudio.com/xiao_pin_multiplexing_esp33c6"
  },
  "name": "Distanz",
  "device-class": "distance",
  "icon": "mdi:ruler",
  "state_class": "measurement",
  "state_topic": "rocket/wasserstand",
  "unique_id": "kaffeemaschine_distanz",
  "unit_of_measurement": "mm",
  "value_template": "{{ value_json.distanz }}"
})rawliteral";

const char* mqtt_ha_config_fuellstand = R"rawliteral({
  "device": {
    "identifiers": [
      "kaffeemaschine"
    ],
    "manufacturer": "Rocket",
    "model": "Appartemento",
    "name": "Kaffeemaschine"
  },
  "enabled_by_default": true,
  "object_id": "kaffeemaschine_fuellstand",
  "origin": {
    "name": "ESP32-C6",
    "sw": "1.0.0",
    "url": "https://wiki.seeedstudio.com/xiao_pin_multiplexing_esp33c6"
  },
  "name": "Füllstand",
  "device_class": "humidity",
  "state_class": "measurement",
  "state_topic": "rocket/wasserstand",
  "unique_id": "kaffeemaschine_fuellstand",
  "unit_of_measurement": "%",
  "value_template": "{{ value_json.fuellstand }}"
})rawliteral";

const char* mqtt_ha_config_command = R"rawliteral({
  "availability": [
    {
      "topic": "rocket/wasserstand/status"
    }
  ],
  "availability_mode": "all",
  "device": {
    "identifiers": [
      "kaffeemaschine"
    ],
    "manufacturer": "Rocket",
    "model": "Appartemento",
    "name": "Kaffeemaschine"
  },
  "enabled_by_default": true,
  "entity_category": "config",
  "object_id": "kaffeemaschine_resetauffuellungen",
  "origin": {
    "name": "ESP32-C6",
    "sw": "1.0.0",
    "url": "https://wiki.seeedstudio.com/xiao_pin_multiplexing_esp33c6"
  },
  "name": "Auffüllungen zurücksetzen",
  "command_topic": "rocket/wasserstand/command",
  "unique_id": "kaffeemaschine_resetauffuellungen",
  "payload_press": "reset_refill_counter"
})rawliteral";

const char* mqtt_ha_config_firmware = R"rawliteral({
  "device": {
    "identifiers": [
      "kaffeemaschine"
    ],
    "manufacturer": "Rocket",
    "model": "Appartemento",
    "name": "Kaffeemaschine"
  },
  "enabled_by_default": true,
  "entity_category": "diagnostic",
  "object_id": "kaffeemaschine_firmware",
  "origin": {
    "name": "ESP32-C6",
    "sw": "1.0.0",
    "url": "https://wiki.seeedstudio.com/xiao_pin_multiplexing_esp33c6"
  },
  "name": "Firmware",
  "state_topic": "rocket/wasserstand",
  "unique_id": "kaffeemaschine_firmware",
  "value_template": "{{ value_json.firmware }}"
})rawliteral";
