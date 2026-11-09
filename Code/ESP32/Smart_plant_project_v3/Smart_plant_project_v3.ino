#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <BH1750.h>
#include <ArduinoJson.h> // Install ArduinoJson 7 using Library Manager.
#include <sys/time.h>
#include <esp_system.h>
#include <esp_timer.h>

// LED WiFi indicator
#define LED_PIN_WIFI LED_BUILTIN

// LED MQTT indicator
#define LED_PIN_MQTT 2

// DHT Sensor
#define DHTPIN 4        // Digital pin connected to the DHT sensor
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);

// 2 Way Module Relay HW-383
#define RELAY1_PIN_1 12 // Submersible Water Pump
#define RELAY1_PIN_2 14 // Water Mist Generator
#define RELAY2_PIN_1 25 // LED Stripe
#define RELAY2_PIN_2 26 // Kipas

// Light Sensor
BH1750 lightMeter;
#define LIGHT_SDA 32
#define LIGHT_SCL 33

// MH Water Sensor (water level)
#define WATER_SENSOR 34

// Capacitive Soil Moisture Sensor
#define SOIL_SENSOR 35
// Calibration values (Update these based on your own sensor testing)
const int AirValue = 2290;   // Sensor reading in dry air
const int WaterValue = 355; // Sensor reading completely submerged in water

// TDS Sensor v1.0
#define TdsSensorPin 39  // ADC pin connected to AOUT
#define VREF 3.3         // ESP32 ADC reference voltage
#define SCOUNT 30        // Number of samples
int analogBuffer[SCOUNT];
int analogBufferIndex = 0;
float averageAnalogRead() {
  long sum = 0;
  for (int i = 0; i < SCOUNT; i++) sum += analogBuffer[i];
  return sum / (float)SCOUNT;
}

// Range ideal suhu tanaman hias 18 - 27 (done)
const int TEMP_THRESHOLD = 27;

// Range ideal kelembaban tanaman hias 60% - 80% (done)
const int MIST_THRESHOLD = 70;

// Range ideal tinggi air pada wadah 250 - 2000 unit (done)
const int WATER_THRESHOLD = 250;

// Range ideal ppm air pada wadah 0 - 450 ppm (done)
const int PPM_THRESHOLD = 450;

// Range ideal kelembaban tanah 50% - 70% (done)
const int SOIL_MOIST_THRESHOLD = 60;

// Range ideal intensitas cahaya 500 - 1500 lux (done)
const int LUX_THRESHOLD = 500;

// Actuator stays ON only for this long (ms) each time it is triggered
const unsigned int PULSE_DURATION = 500; // 0.5 detik

const unsigned long STATE_NOTIF_INTERVAL = 300000; // 5 menit
unsigned long lastStateNotif = 0;

// WiFi Connection
const char* ssid = "Kantin Belakang";
const char* password = "PemudaTersesat27";

// MQTT Server
const bool isCloud = false;
const char* cloudMqttServer = "broker.hivemq.com"; // MQTT cloud broker
const int cloudMqttPort = 1883; // MQTT cloud port 
const char* localMqttServer = "192.168.1.29"; // MQTT local broker
const int localMqttPort = 1883; // MQTT local port

WiFiClient espClient;
PubSubClient client(espClient);

// MQTT Topics
const char* PUB_TEMPERATURE  = "/smartplant/esp32-01/sensor/temperature";
const char* PUB_HUMIDITY     = "/smartplant/esp32-01/sensor/humidity";
const char* PUB_MOISTURE     = "/smartplant/esp32-01/sensor/moisture";
const char* PUB_WATER_LEVEL  = "/smartplant/esp32-01/sensor/water-level";
const char* PUB_LIGHT        = "/smartplant/esp32-01/sensor/light";
const char* PUB_TDS          = "/smartplant/esp32-01/sensor/ppm";
const char* PUB_SNAPSHOT     = "/smartplant/esp32-01/data";

const char* NOTIF_PUMP       = "/smartplant/esp32-01/notification/pump";
const char* NOTIF_MIST       = "/smartplant/esp32-01/notification/mist";
const char* NOTIF_LED        = "/smartplant/esp32-01/notification/led";
const char* NOTIF_FAN        = "/smartplant/esp32-01/notification/fan";

// Plain-text mode requests: off / rule_based / predictive (publish non-retained).
const char* CONTROL_MODE = "/smartplant/esp32-01/control/mode";
const char* STATUS_CONTROL = "/smartplant/esp32-01/status/control";
const char* CONTROL_PUMP = "/smartplant/esp32-01/control/pump";
const char* STATUS_PUMP_COMMAND = "/smartplant/esp32-01/status/pump-command";
const unsigned long PUMP_COOLDOWN_MS = 300000;
const unsigned long CONTROL_SENSOR_MAX_AGE_MS = 15000;
String controlSession;
unsigned long lastControlSensorRead = 0;
bool controlSensorsReady = false;
bool hasWatered = false;
unsigned long lastWateringStart = 0;
double lastAcceptedOriginMs = 0;
unsigned int tdsSampleCount = 0;
esp_timer_handle_t pumpOffTimer = nullptr;
enum class PumpControlMode { Off, RuleBased, Predictive };
PumpControlMode pumpControlMode = PumpControlMode::Off; // Explicit selection after reboot.

const char* ON               = "1"; // ON
const char* OFF              = "0"; // OFF

void publishTopic(const char* topicName, String value){
  if(client.publish(topicName, value.c_str())) {
    Serial.print("Published [");
    Serial.print(topicName);
    Serial.print("]: ");
    Serial.println(value);
  } else {
    Serial.print("Failed to publish [");
    Serial.print(topicName);
    Serial.println("]");
  };
}

void setup_wifi() {
  delay(100);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    digitalWrite(LED_PIN_WIFI, LOW); // LED OFF
    Serial.print(".");
    Serial.print(" status=");
    Serial.println(WiFi.status());
    
  }

  digitalWrite(LED_PIN_WIFI, HIGH); // LED ON
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup_light_sensor() {
  Wire.begin(LIGHT_SDA, LIGHT_SCL); // SDA, SCL pins for ESP32

  // Initialize BH1750
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 sensor initialized successfully.");
  } else {
    Serial.println("Error initializing BH1750. Check wiring!");
    while (true) delay(100);
  }
}

int latestWaterLevel = -1;
float latestTdsValue = -1;
float latestSoilMoistureValue = -1;
float latestTemperatureValue = -1;
float latestAirHumidityValue = -1;
float latestLuxValue = -1;

// Pulse state of each actuator
bool isPumpPulseActive = false;
bool isMistPulseActive = false;
bool isLedPulseActive = false;
bool isFanPulseActive = false;

unsigned long pumpPulseStart = 0;
unsigned long mistPulseStart = 0;
unsigned long ledPulseStart = 0;
unsigned long fanPulseStart = 0;

// Runs in the ESP timer task, even if MQTT network I/O blocks the Arduino loop.
// No network calls here. The main loop publishes the OFF notification afterwards.
void pumpOffCallback(void*) {
  digitalWrite(RELAY1_PIN_1, HIGH);
}

double epochMilliseconds() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (double)tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

bool clockReady() {
  return epochMilliseconds() >= 1767225600000.0; // 2026-01-01 UTC
}

unsigned long pumpCooldownRemaining() {
  unsigned long elapsed = millis() - lastWateringStart;
  return hasWatered && elapsed < PUMP_COOLDOWN_MS ? PUMP_COOLDOWN_MS - elapsed : 0;
}

bool pumpSensorsSafe() {
  return controlSensorsReady && millis() - lastControlSensorRead <= CONTROL_SENSOR_MAX_AGE_MS
    && latestWaterLevel >= WATER_THRESHOLD && latestWaterLevel < 4095
    && isfinite(latestTdsValue) && latestTdsValue >= 0 && latestTdsValue <= PPM_THRESHOLD
    && isfinite(latestSoilMoistureValue) && latestSoilMoistureValue >= 0
    && latestSoilMoistureValue < 70;
}

void renewControlSession() {
  controlSession = String(esp_random(), HEX) + "-" + String(esp_random(), HEX);
}

const char* pumpModeName() {
  switch (pumpControlMode) {
    case PumpControlMode::RuleBased: return "rule_based";
    case PumpControlMode::Predictive: return "predictive";
    default: return "off";
  }
}

void stopPump() {
  if (pumpOffTimer) esp_timer_stop(pumpOffTimer);
  bool wasOn = isPumpPulseActive || digitalRead(RELAY1_PIN_1) == LOW;
  digitalWrite(RELAY1_PIN_1, HIGH);
  isPumpPulseActive = false;
  if (wasOn && client.connected()) publishTopic(NOTIF_PUMP, OFF);
}

void publishControlStatus() {
  JsonDocument doc;
  doc["mode"] = pumpModeName();
  doc["session"] = controlSession;
  doc["clock_ready"] = clockReady();
  doc["sensors_safe"] = pumpSensorsSafe();
  doc["cooldown_remaining_ms"] = pumpCooldownRemaining();
  doc["pump_on"] = digitalRead(RELAY1_PIN_1) == LOW;
  doc["predictive_ready"] = pumpControlMode == PumpControlMode::Predictive
    && clockReady() && pumpSensorsSafe() && pumpCooldownRemaining() == 0
    && !isPumpPulseActive && pumpOffTimer != nullptr;
  String status;
  serializeJson(doc, status);
  publishTopic(STATUS_CONTROL, status);
}

void commandReply(const char* id, const char* result, const char* reason) {
  JsonDocument reply;
  reply["command_id"] = id;
  reply["result"] = result;
  reply["reason"] = reason;
  reply["mode"] = pumpModeName();
  reply["pump_on"] = digitalRead(RELAY1_PIN_1) == LOW;
  String payload;
  serializeJson(reply, payload);
  publishTopic(STATUS_PUMP_COMMAND, payload);
}

void handlePumpCommand(byte* payload, unsigned int length) {
  if (length > 900) { commandReply("", "rejected", "payload_too_large"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) {
    commandReply("", "rejected", "invalid_json"); return;
  }
  const char* id = doc["command_id"] | "";
  const char* session = doc["session"] | "";
  if (strlen(id) == 0 || strlen(id) > 120 || controlSession != session) {
    commandReply("", "rejected", "invalid_id_or_session"); return;
  }
  if (pumpControlMode != PumpControlMode::Predictive) {
    commandReply(id, "rejected", "wrong_mode"); return;
  }
  if (!clockReady()) { commandReply(id, "rejected", "clock_not_ready"); return; }
  if (!doc["issued_at_epoch_ms"].is<double>() || !doc["expires_at_epoch_ms"].is<double>()
      || !doc["origin_epoch_ms"].is<double>() || !doc["predicted_moisture"].is<double>()
      || !doc["duration_ms"].is<unsigned int>()) {
    commandReply(id, "rejected", "invalid_numeric_fields"); return;
  }
  double nowMs = epochMilliseconds();
  double issued = doc["issued_at_epoch_ms"].as<double>();
  double expires = doc["expires_at_epoch_ms"].as<double>();
  double origin = doc["origin_epoch_ms"].as<double>();
  double predicted = doc["predicted_moisture"].as<double>();
  unsigned int duration = doc["duration_ms"].as<unsigned int>();
  if (!isfinite(issued) || !isfinite(expires) || !isfinite(origin)
      || issued > nowMs + 2000 || nowMs >= expires || expires <= issued
      || expires - issued > 15000 || nowMs - issued > 15000
      || origin > nowMs + 2000 || nowMs - origin > 90000) {
    commandReply(id, "rejected", "expired_or_invalid_time"); return;
  }
  if (origin <= lastAcceptedOriginMs) {
    commandReply(id, "rejected", "duplicate_or_old_forecast"); return;
  }
  if (strcmp(doc["action"] | "", "pulse") != 0
      || strcmp(doc["model"] | "", "gradient_boosting") != 0
      || strcmp(doc["horizon"] | "", "10m") != 0
      || !isfinite(predicted) || predicted < 0 || predicted > SOIL_MOIST_THRESHOLD
      || duration == 0 || duration > PULSE_DURATION) {
    commandReply(id, "rejected", "invalid_policy_or_duration"); return;
  }
  if (!pumpSensorsSafe()) { commandReply(id, "rejected", "sensor_interlock"); return; }
  if (isPumpPulseActive || pumpCooldownRemaining() > 0) {
    commandReply(id, "rejected", "busy_or_cooldown"); return;
  }
  if (!pumpOffTimer || esp_timer_start_once(pumpOffTimer, duration * 1000ULL) != ESP_OK) {
    commandReply(id, "rejected", "pulse_timer_unavailable"); return;
  }
  lastAcceptedOriginMs = origin;
  hasWatered = true;
  lastWateringStart = millis();
  pumpPulseStart = millis();
  isPumpPulseActive = true;
  digitalWrite(RELAY1_PIN_1, LOW);
  // Timer may turn the pin off during network calls below.
  publishTopic(NOTIF_PUMP, ON);
  commandReply(id, "accepted", "pulse_started");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, CONTROL_PUMP) == 0) { handlePumpCommand(payload, length); return; }
  if (strcmp(topic, CONTROL_MODE) != 0) return;
  if (length == 0 || length >= 20) {
    Serial.println("Rejected invalid pump mode payload length");
    return;
  }
  // MQTT payload is not null-terminated. Parse using its explicit length.
  String requested;
  for (unsigned int i = 0; i < length; i++) requested += (char)payload[i];
  PumpControlMode nextMode;
  if (requested == "off") nextMode = PumpControlMode::Off;
  else if (requested == "rule_based") nextMode = PumpControlMode::RuleBased;
  else if (requested == "predictive") nextMode = PumpControlMode::Predictive;
  else {
    Serial.println("Rejected unknown pump control mode");
    publishControlStatus();
    return;
  }
  // A mode request always stops an active pulse before applying the mode.
  stopPump();
  pumpControlMode = nextMode;
  renewControlSession(); // Invalidate queued commands from the previous mode.
  publishControlStatus();
}

void updatePumpState() {
  // Only the rule-based mode may trigger the existing threshold controller.
  if (pumpControlMode != PumpControlMode::RuleBased) return;
  if (latestWaterLevel < 0 || latestTdsValue < 0 || latestSoilMoistureValue < 0) {
    return;
  }

  bool isSoilBelowRange = latestSoilMoistureValue <= SOIL_MOIST_THRESHOLD;
  bool isWaterLevelInRange = latestWaterLevel >= WATER_THRESHOLD;
  bool isTdsInRange = latestTdsValue <= PPM_THRESHOLD;

  bool isPumpAllowed = isSoilBelowRange && isWaterLevelInRange && isTdsInRange;

  if (isPumpAllowed && !isPumpPulseActive && pumpOffTimer
      && esp_timer_start_once(pumpOffTimer, PULSE_DURATION * 1000ULL) == ESP_OK) {
    hasWatered = true;
    lastWateringStart = millis();
    digitalWrite(RELAY1_PIN_1, LOW); // Water pump on
    isPumpPulseActive = true;
    pumpPulseStart = millis();
    publishTopic(NOTIF_PUMP, ON);
  } else if (!isPumpAllowed && isPumpPulseActive) {
    digitalWrite(RELAY1_PIN_1, HIGH); // Water pump off (conditions no longer safe)
    isPumpPulseActive = false;
    publishTopic(NOTIF_PUMP, OFF);
  }

  if (isPumpPulseActive) {
    Serial.println("Water Pump: ON");
  } else {
    Serial.println("Water Pump: OFF");
  }
}

void updateMistState() {
  if (latestWaterLevel < 0 || latestAirHumidityValue < 0 || latestTemperatureValue < 0 ) {
    return;
  }

  bool isWaterLevelInRange = latestWaterLevel >= WATER_THRESHOLD;
  bool isHumidityBelowRange = latestAirHumidityValue <= MIST_THRESHOLD;

  bool isMistAllowed = isWaterLevelInRange && isHumidityBelowRange;

  if (isMistAllowed && !isMistPulseActive) {
    digitalWrite(RELAY1_PIN_2, LOW); // Water mist on
    isMistPulseActive = true;
    mistPulseStart = millis();
    publishTopic(NOTIF_MIST, ON);
  } else if (!isMistAllowed && isMistPulseActive) {
    digitalWrite(RELAY1_PIN_2, HIGH); // Water mist off (conditions no longer safe)
    isMistPulseActive = false;
    publishTopic(NOTIF_MIST, OFF);
  }

  if (isMistPulseActive) {
    Serial.println("Water Mist: ON");
  } else {
    Serial.println("Water Mist: OFF");
  }
}

void updateLedState() {
  if (latestLuxValue < 0) {
    return;
  }

  bool isLuxBelow = latestLuxValue <= LUX_THRESHOLD;
  bool isLedAllowed = isLuxBelow;

  if (isLedAllowed && !isLedPulseActive) {
    digitalWrite(RELAY2_PIN_1, LOW); // LED on
    isLedPulseActive = true;
    ledPulseStart = millis();
    publishTopic(NOTIF_LED, ON);
  } else if (!isLedAllowed && isLedPulseActive) {
    digitalWrite(RELAY2_PIN_1, HIGH); // LED off (conditions no longer met)
    isLedPulseActive = false;
    publishTopic(NOTIF_LED, OFF);
  }

  if (isLedPulseActive) {
    Serial.println("LED: ON");
  } else {
    Serial.println("LED: OFF");
  }
}

void updateFanState() {
  if (latestTemperatureValue < 0) {
    return;
  }

  bool isTemperatureOver = latestTemperatureValue >= TEMP_THRESHOLD;
  bool isFanAllowed = isTemperatureOver;

  if (isFanAllowed && !isFanPulseActive) {
    digitalWrite(RELAY2_PIN_2, LOW); // Fan on
    isFanPulseActive = true;
    fanPulseStart = millis();
    publishTopic(NOTIF_FAN, ON);
  } else if (!isFanAllowed && isFanPulseActive) {
    digitalWrite(RELAY2_PIN_2, HIGH); // Fan off (conditions no longer met)
    isFanPulseActive = false;
    publishTopic(NOTIF_FAN, OFF);
  }

  if (isFanPulseActive) {
    Serial.println("FAN: ON");
  } else {
    Serial.println("FAN: OFF");
  }
}

// Turn off every actuator whose pulse has lasted PULSE_DURATION
void checkPulseTimers() {
  unsigned long now = millis();

  if (isPumpPulseActive && (digitalRead(RELAY1_PIN_1) == HIGH
      || now - pumpPulseStart >= PULSE_DURATION)) {
    stopPump();
  }
}

// Publish topic untuk kondisi aktuator
void publishActuatorStates() {
  bool isPumpOn = digitalRead(RELAY1_PIN_1) == LOW;
  bool isMistOn = digitalRead(RELAY1_PIN_2) == LOW;
  bool isLedOn = digitalRead(RELAY2_PIN_1) == LOW;
  bool isFanOn = digitalRead(RELAY2_PIN_2) == LOW;

  publishTopic(NOTIF_PUMP, isPumpOn ? ON : OFF);
  publishTopic(NOTIF_MIST, isMistOn ? ON : OFF);
  publishTopic(NOTIF_LED, isLedOn ? ON : OFF);
  publishTopic(NOTIF_FAN, isFanOn ? ON : OFF);
}

void publishSnapshot() {
  bool pumpOn = digitalRead(RELAY1_PIN_1) == LOW;
  bool mistOn = digitalRead(RELAY1_PIN_2) == LOW;
  bool ledOn  = digitalRead(RELAY2_PIN_1) == LOW;
  bool fanOn  = digitalRead(RELAY2_PIN_2) == LOW;

  String payload = "{";

  payload += "\"device\":\"esp32-01\",";

  payload += "\"temperature\":";
  payload += String(latestTemperatureValue, 2);
  payload += ",";

  payload += "\"humidity\":";
  payload += String(latestAirHumidityValue, 2);
  payload += ",";

  payload += "\"moisture\":";
  payload += String(latestSoilMoistureValue, 2);
  payload += ",";

  payload += "\"water_level\":";
  payload += String(latestWaterLevel);
  payload += ",";

  payload += "\"light\":";
  payload += String(latestLuxValue, 2);
  payload += ",";

  payload += "\"tds\":";
  payload += String(latestTdsValue, 2);
  payload += ",";

  payload += "\"pump\":";
  payload += pumpOn ? "1" : "0";
  payload += ",";

  payload += "\"mist\":";
  payload += mistOn ? "1" : "0";
  payload += ",";

  payload += "\"led\":";
  payload += ledOn ? "1" : "0";
  payload += ",";

  payload += "\"fan\":";
  payload += fanOn ? "1" : "0";

  payload += "}";

  publishTopic(PUB_SNAPSHOT, payload);
}

void setup() {
  Serial.begin(9600);

  pinMode(WATER_SENSOR, INPUT); // Water Level Sensor

  pinMode(LED_PIN_WIFI, OUTPUT); // WiFi indicator LED
  pinMode(LED_PIN_MQTT, OUTPUT); // MQTT indicator LED

  pinMode(RELAY1_PIN_1, OUTPUT); // Relay 1 1
  pinMode(RELAY1_PIN_2, OUTPUT); // Relay 1 2
  pinMode(RELAY2_PIN_1, OUTPUT); // Relay 2 1
  pinMode(RELAY2_PIN_2, OUTPUT); // Relay 2 2

  digitalWrite(RELAY1_PIN_1, HIGH); // Pump off; relay is active-low
  digitalWrite(RELAY1_PIN_2, HIGH); // Water Mist Generator off; relay is active-low
  digitalWrite(RELAY2_PIN_1, HIGH); // LED off; relay is active-low
  digitalWrite(RELAY2_PIN_2, HIGH); // Fan off; relay is active-low

  esp_timer_create_args_t timerArgs = {};
  timerArgs.callback = pumpOffCallback;
  timerArgs.name = "pump_off";
  if (esp_timer_create(&timerArgs, &pumpOffTimer) != ESP_OK) {
    pumpOffTimer = nullptr; // Both pump controllers fail closed if timer creation fails.
  }

  setup_wifi();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // START MQTT Setup
  const char* mqttServer = isCloud ? cloudMqttServer : localMqttServer;
  const int mqttPort = isCloud ? cloudMqttPort : localMqttPort;
  
  client.setServer(mqttServer, mqttPort);
  client.setCallback(mqttCallback);
  client.setBufferSize(1536);
  client.setSocketTimeout(2);
  // END MQTT Setup

  dht.begin();
  setup_light_sensor();
}

void reconnect() {
  static unsigned long lastAttempt = 0;
  static bool attempted = false;
  if (attempted && millis() - lastAttempt < 5000) return;
  attempted = true;
  lastAttempt = millis();
  if (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESPClient")) {
      renewControlSession();
      client.subscribe(CONTROL_MODE);
      client.subscribe(CONTROL_PUMP);
      publishActuatorStates();
      publishControlStatus();
      Serial.println("connected");
      digitalWrite(LED_PIN_MQTT, HIGH);
    } else {
      digitalWrite(LED_PIN_MQTT, LOW);
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds"); 
    }
  }
}

void dht22Sensor() {
  // Read humidity and temperature
  float h = dht.readHumidity();
  float t = dht.readTemperature(); // Celsius by default

  // Check if readings failed
  if (isnan(h) || isnan(t)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }
  
  latestAirHumidityValue = h;
  latestTemperatureValue = t;
  publishTopic(PUB_TEMPERATURE, String(t, 2));
  publishTopic(PUB_HUMIDITY, String(h, 2));

  // Serial.print(F("Humidity: "));
  // Serial.print(h);
  // Serial.print(F("% | Temperature: "));
  // Serial.print(t);
  // Serial.println(F("°C"));
}

void capacitiveSoilSensor() {
  int sensorVal = analogRead(SOIL_SENSOR); // Read analog value from ESP32 pin
  
  // Convert raw reading to a percentage (constrained between 0% and 100%)
  int moisturePercent = map(sensorVal, AirValue, WaterValue, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  latestSoilMoistureValue = moisturePercent;
  publishTopic(PUB_MOISTURE, String(moisturePercent));

  // Serial.print("Raw Value: ");
  // Serial.print(sensorVal);
  // Serial.print(" | AirValue: ");
  // Serial.print(AirValue);
  // Serial.print(" | WaterValue: ");
  // Serial.print(WaterValue);
  // Serial.print(" | Moisture: ");
  // Serial.print(moisturePercent);
  // Serial.println("%");
}

void waterLevelSensor() {
  int waterLevel = analogRead(WATER_SENSOR);
  latestWaterLevel = waterLevel;
  publishTopic(PUB_WATER_LEVEL, String(waterLevel));

  // Serial.print("Water Level Value: ");
  // Serial.println(latestWaterLevel);
}

void lightLevelSensor() {
  // Read light level in lux
  float lux = lightMeter.readLightLevel();

  // Validate reading
  if (lux < 0) {
    Serial.println("Error reading light level.");
  } else {
    latestLuxValue = lux;
    publishTopic(PUB_LIGHT, String(lux, 2));

    // Serial.print("Light: ");
    // Serial.print(lux);
    // Serial.println(" lx");
  }
}

void tdsMeterSensor() {
  analogBuffer[analogBufferIndex++] = analogRead(TdsSensorPin);
  if (tdsSampleCount < SCOUNT) tdsSampleCount++;
  if (analogBufferIndex == SCOUNT) analogBufferIndex = 0;

  float average = averageAnalogRead();
  float voltage = average * (VREF / 4095.0); // ESP32 ADC is 12-bit
  float tdsValue = (133.42 * voltage * voltage * voltage
                   - 255.86 * voltage * voltage
                   + 857.39 * voltage) * 0.5; // ppm

  latestTdsValue = tdsValue;
  publishTopic(PUB_TDS, String(tdsValue, 2));

  // Serial.print("Voltage: ");
  // Serial.print(voltage, 2);
  // Serial.print(" V  |  TDS: ");
  // Serial.print(tdsValue, 0);
  // Serial.println(" ppm");
}

const unsigned long SENSOR_INTERVAL = 5000;
unsigned long lastSensorRead = 0;

void loop() {
  // Never enter the existing blocking reconnect loop with the pump energized.
  checkPulseTimers();
  if (!client.connected()) {
    stopPump();
    reconnect();
    if (!client.connected()) return;
  }

  // Process incoming MQTT messages first
  client.loop();
  if (!client.connected()) { stopPump(); return; }

  // Turn off actuators that have been ON for PULSE_DURATION (runs every loop, not every 2 seconds)
  checkPulseTimers();

  unsigned long now = millis();

  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    dht22Sensor();
    capacitiveSoilSensor();
    waterLevelSensor();
    lightLevelSensor();
    tdsMeterSensor();

    lastControlSensorRead = millis();
    controlSensorsReady = tdsSampleCount >= SCOUNT;
    if (pumpControlMode == PumpControlMode::Predictive && !pumpSensorsSafe()) stopPump();

    updatePumpState();
    updateMistState();
    updateLedState();
    updateFanState();
    publishControlStatus();
    publishSnapshot();
    Serial.println("");
  }

  // Send actuator state every 5 minutes
  if (now - lastStateNotif >= STATE_NOTIF_INTERVAL) {
    lastStateNotif = now;
    publishActuatorStates();
  }
}
