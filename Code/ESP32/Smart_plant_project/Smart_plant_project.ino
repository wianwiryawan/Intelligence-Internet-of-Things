#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_NeoPixel.h>

// LED WiFi indicator
#define LED_PIN_WIFI 0

// LED MQTT indicator
#define LED_PIN_MQTT 2

// 2 Way Module Relay HW-383
#define RELAY1_PIN_1 12 // Submersible Water Pump
#define RELAY1_PIN_2 14 // Water Mist Generator
#define RELAY2_PIN_1 25 // LED
#define RELAY2_PIN_2 26 // Solenoid Valve

// DHT Sensor
#define DHTPIN 4        // Digital pin connected to the DHT sensor
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);
// Range ideal suhu tanaman hias 18 - 27
const int LED_ON_TEMP_LEVEL = 18;
const int LED_OFF_TEMP_LEVEL = 27;
// Range ideal kelembaban tanaman hias 60% - 80%
const int MIST_ON_LEVEL = 60;
const int MIST_OFF_LEVEL = 80;

// Capacitive Soil Moisture Sensor
#define SOIL_SENSOR 35

// Calibration values (Update these based on your own sensor testing)
const int AirValue = 2290;   // Sensor reading in dry air
const int WaterValue = 355; // Sensor reading completely submerged in water

// MH Water Sensor (water level)
#define WATER_SENSOR 34
// Range ideal tinggi air pada wadah 1000 - 1500 unit
const int PUMP_ON_WATER_LEVEL = 1000;
const int PUMP_OFF_WATER_LEVEL = 2000;
// Range ideal tinggi air pada wadah 1000 - 1500 unit
const int SOLENOID_ON_WATER_LEVEL = 1000;
const int SOLENOID_OFF_WATER_LEVEL = 1500;
// Range ideal ppm air pada wadah 50 - 300 ppm
const int PUMP_ON_PPM_LEVEL = 50;
const int PUMP_OFF_PPM_LEVEL = 300;
// Range ideal kelembaban tanah 50% - 70%
const int PUMP_ON_SOL_MOIST_LEVEL = 50;
const int PUMP_OFF_SOL_MOIST_LEVEL = 70;

// Light Sensor
BH1750 lightMeter;
#define LIGHT_SDA 32
#define LIGHT_SCL 33
// Range ideal lux untuk tanaman 2000 - 5000 ppm
const int LED_ON_LUX_LEVEL = 2000;
const int LED_OFF_LUX_LEVEL = 5000;

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

// WiFi Connection
const char* ssid = "Kantin Belakang";
const char* password = "PemudaTersesat27";

// MQTT Server
const bool isCloud = false;
const char* cloudMqttServer = "broker.hivemq.com"; // MQTT cloud broker
const int cloudMqttPort = 1883; // MQTT cloud port 
const char* localMqttServer = "192.168.1.50"; // MQTT local broker
const int localMqttPort = 1883; // MQTT local port

WiFiClient espClient;
PubSubClient client(espClient);

// MQTT Topics
// /smartplant/<device>/<category>/<name>
const char* SUB_RELAY_1_1   = "/smartplant/esp32-01/command/relay/1/1"; // Submersible Water Pump
const char* SUB_RELAY_1_2   = "/smartplant/esp32-01/command/relay/1/2"; // Water Mist Generator
const char* SUB_RELAY_2_1   = "/smartplant/esp32-01/command/relay/2/1"; // LED
const char* SUB_RELAY_2_2   = "/smartplant/esp32-01/command/relay/2/2"; // Solenoid Valve

const char* PUB_TEMPERATURE  = "/smartplant/esp32-01/sensor/temperature";
const char* PUB_HUMIDITY     = "/smartplant/esp32-01/sensor/humidity";
const char* PUB_MOISTURE     = "/smartplant/esp32-01/sensor/moisture";
const char* PUB_WATER_LEVEL  = "/smartplant/esp32-01/sensor/water-level";
const char* PUB_LIGHT        = "/smartplant/esp32-01/sensor/light";
const char* PUB_TDS          = "/smartplant/esp32-01/sensor/ppm";

const char* NOTIF_PUMP       = "/smartplant/esp32-01/notification/pump";
const char* NOTIF_MIST       = "/smartplant/esp32-01/notification/mist";
const char* NOTIF_LED        = "/smartplant/esp32-01/notification/led";
const char* NOTIF_SOLENOID   = "/smartplant/esp32-01/notification/solenoid";

const char* AKTIF            = "Aktif";
const char* MATI             = "Mati";
const char* OPEN             = "Open";
const char* CLOSE            = "Close";

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

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String string;
  for (int i = 0; i < length; i++) {
    string+=((char)payload[i]);  
  }
  Serial.println(string);

  if (String(topic) == SUB_RELAY_1_1) {
    if (string == "1") {
      digitalWrite(RELAY1_PIN_1, LOW);
      publishTopic(NOTIF_PUMP, AKTIF);
    } else {
      digitalWrite(RELAY1_PIN_1, HIGH);
      publishTopic(NOTIF_PUMP, MATI);
    }
  } else if (String(topic) == SUB_RELAY_1_2) {
    if (string == "1") {
      digitalWrite(RELAY1_PIN_2, LOW);
      publishTopic(NOTIF_MIST, AKTIF);
    } else {
      digitalWrite(RELAY1_PIN_2, HIGH);
      publishTopic(NOTIF_MIST, MATI);
    }
  } else if (String(topic) == SUB_RELAY_2_1) {
    if (string == "1") {
      digitalWrite(RELAY2_PIN_1, LOW);
      publishTopic(NOTIF_LED, AKTIF);
    } else {
      digitalWrite(RELAY2_PIN_1, HIGH);
      publishTopic(NOTIF_LED, MATI);
    } 
  } else if (String(topic) == SUB_RELAY_2_2) {
    if (string == "1") {
      digitalWrite(RELAY2_PIN_2, LOW);
      publishTopic(NOTIF_SOLENOID, OPEN);
    } else {
      digitalWrite(RELAY2_PIN_2, HIGH);
      publishTopic(NOTIF_SOLENOID, CLOSE);
    }
  }
}

int latestWaterLevel = -1;
float latestTdsValue = -1;
float latestSoilMoistureValue = -1;
float latestTemperatureValue = -1;
float latestAirHumidityValue = -1;
float latestLuxValue = -1;
bool solenoidOpen = false;

void updatePumpState() {
  if (latestWaterLevel < 0 || latestTdsValue < 0 || latestSoilMoistureValue < 0) {
    return;
  }

  bool isWaterLevelInRange = latestWaterLevel >= PUMP_ON_WATER_LEVEL
                          && latestWaterLevel <= PUMP_OFF_WATER_LEVEL;
  bool isWaterOver = latestWaterLevel >= PUMP_OFF_WATER_LEVEL;

  bool isTdsInRange = latestTdsValue >= PUMP_ON_PPM_LEVEL
                    && latestTdsValue <= PUMP_OFF_PPM_LEVEL;
  bool isTdsOver = latestTdsValue >= PUMP_OFF_PPM_LEVEL;

  bool isSoilInRange = latestSoilMoistureValue >= PUMP_ON_SOL_MOIST_LEVEL
                    && latestSoilMoistureValue <= PUMP_OFF_SOL_MOIST_LEVEL;
  bool isSoilOver = latestSoilMoistureValue >= PUMP_OFF_SOL_MOIST_LEVEL;

  if (isWaterLevelInRange && isTdsInRange) {
    if (isSoilInRange || isSoilOver){
      digitalWrite(RELAY1_PIN_1, HIGH); // Water pump off
      publishTopic(NOTIF_PUMP, MATI);
      Serial.println("Pump: OFF");
    } else {
      digitalWrite(RELAY1_PIN_1, LOW); // Water pump on
      publishTopic(NOTIF_PUMP, AKTIF);
      Serial.println("Pump: ON");
    }
  } else {
    digitalWrite(RELAY1_PIN_1, HIGH); // Water pump off
    publishTopic(NOTIF_PUMP, MATI);
    Serial.println("Pump: OFF");
  }
}

void updateLedState() {
  if (latestTemperatureValue < 0 || latestLuxValue < 0 ) {
    return;
  }

  bool isTemperatureInRange = latestTemperatureValue >= LED_ON_TEMP_LEVEL
                            && latestTemperatureValue <= LED_OFF_TEMP_LEVEL;
  bool isTemperatureOver = latestTemperatureValue >= LED_OFF_TEMP_LEVEL;

  bool isLuxInRange = latestLuxValue >= LED_ON_LUX_LEVEL
                    && latestLuxValue <= LED_OFF_LUX_LEVEL;
  bool isLuxOver = latestLuxValue >= LED_OFF_LUX_LEVEL;

  if (isTemperatureInRange && isLuxInRange) {
    digitalWrite(RELAY2_PIN_1, HIGH);
    publishTopic(NOTIF_LED, MATI);
    Serial.println("LED: OFF");
  } else if (isLuxOver || isTemperatureOver){
    digitalWrite(RELAY2_PIN_1, HIGH);
    publishTopic(NOTIF_LED, MATI);
    Serial.println("LED: OFF");
  } else {
    digitalWrite(RELAY2_PIN_1, LOW);
    publishTopic(NOTIF_LED, AKTIF);
    Serial.println("LED: ON");
  }
}

void updateSolenoidState(int latestWaterLevel) {
  // CLOSED -> OPEN
  if (!solenoidOpen && latestWaterLevel <= SOLENOID_ON_WATER_LEVEL) {
    solenoidOpen = true;

    digitalWrite(RELAY2_PIN_2, LOW); // Relay ON
    publishTopic(NOTIF_SOLENOID, OPEN);

    Serial.println("Solenoid: OPEN");
  }

  // OPEN -> CLOSED
  else if (solenoidOpen && latestWaterLevel >= SOLENOID_OFF_WATER_LEVEL) {
    solenoidOpen = false;

    digitalWrite(RELAY2_PIN_2, HIGH); // Relay OFF
    publishTopic(NOTIF_SOLENOID, CLOSE);

    Serial.println("Solenoid: CLOSE");
  }
}

void subscribeTopics() {
  bool sub1 = client.subscribe(SUB_RELAY_1_1);
  bool sub2 = client.subscribe(SUB_RELAY_1_2);
  bool sub3 = client.subscribe(SUB_RELAY_2_1);
  bool sub4 = client.subscribe(SUB_RELAY_2_2);

  Serial.print("Subscribe relay 1 pin 1: ");
  Serial.println(sub1 ? "OK" : "FAILED");

  Serial.print("Subscribe relay 1 pin 2: ");
  Serial.println(sub2 ? "OK" : "FAILED");

  Serial.print("Subscribe relay 2 pin 1: ");
  Serial.println(sub3 ? "OK" : "FAILED");

  Serial.print("Subscribe relay 2 pin 1: ");
  Serial.println(sub4 ? "OK" : "FAILED");
}

void setup() {
  Serial.begin(9600);

  pinMode(LED_PIN_WIFI, OUTPUT); // WiFi indicator LED
  pinMode(LED_PIN_MQTT, OUTPUT); // MQTT indicator LED

  pinMode(RELAY1_PIN_1, OUTPUT); // Relay 1 1
  pinMode(RELAY1_PIN_2, OUTPUT); // Relay 1 2
  pinMode(RELAY2_PIN_1, OUTPUT); // Relay 2 1
  pinMode(RELAY2_PIN_2, OUTPUT); // Relay 2 2

  digitalWrite(RELAY1_PIN_1, HIGH); // Pump off; relay is active-low
  digitalWrite(RELAY1_PIN_2, HIGH); // Water Mist Generator off; relay is active-low
  digitalWrite(RELAY2_PIN_1, HIGH); // LED off; relay is active-low
  digitalWrite(RELAY2_PIN_2, HIGH); // Solenoid Valve off; relay is active-low

  // START WiFi Setup
  setup_wifi();
  // END WiFi Setup

  // START MQTT Setup
  const char* mqttServer = isCloud ? cloudMqttServer : localMqttServer;
  const int mqttPort = isCloud ? cloudMqttPort : localMqttPort;
  
  client.setServer(mqttServer, mqttPort);
  client.setCallback(mqtt_callback);
  // END MQTT Setup

  // START DHT22 Setup
  dht.begin();
  // START DHT22 Setup

  pinMode(WATER_SENSOR, INPUT);

  // START BH1750 Setup
  setup_light_sensor();
  // START BH1750 Setup
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESPClient")) {
      subscribeTopics();
      Serial.println("connected");
      digitalWrite(LED_PIN_MQTT, HIGH);
    } else {
      digitalWrite(LED_PIN_MQTT, LOW);
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds"); 
      delay(5000);
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

  Serial.print(F("Humidity: "));
  Serial.print(h);
  Serial.print(F("% | Temperature: "));
  Serial.print(t);
  Serial.println(F("°C"));

  if (latestAirHumidityValue <= MIST_ON_LEVEL) {
      digitalWrite(RELAY1_PIN_2, LOW);
      publishTopic(NOTIF_MIST, AKTIF);
      Serial.println("Water Mist: ON");
  } else if (latestAirHumidityValue >= MIST_OFF_LEVEL) {
      digitalWrite(RELAY1_PIN_2, HIGH);
      publishTopic(NOTIF_MIST, MATI);
      Serial.println("Water Mist: OFF");
  } else {
      digitalWrite(RELAY1_PIN_2, HIGH);
      publishTopic(NOTIF_MIST, MATI);
      Serial.println("Water Mist: OFF");
  }
}

void capacitiveSoilSensor() {
  int sensorVal = analogRead(SOIL_SENSOR); // Read analog value from ESP32 pin
  
  // Convert raw reading to a percentage (constrained between 0% and 100%)
  int moisturePercent = map(sensorVal, AirValue, WaterValue, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  latestSoilMoistureValue = moisturePercent;
  publishTopic(PUB_MOISTURE, String(moisturePercent));

  Serial.print("Raw Value: ");
  Serial.print(sensorVal);
  Serial.print(" | AirValue: ");
  Serial.print(AirValue);
  Serial.print(" | WaterValue: ");
  Serial.print(WaterValue);
  Serial.print(" | Moisture: ");
  Serial.print(moisturePercent);
  Serial.println("%");
}

void waterLevelSensor() {
  int waterLevel = analogRead(WATER_SENSOR);

  if(isnan(waterLevel)) {
    Serial.println(F("Failed to read from water level sensor!"));
    return;
  }

  latestWaterLevel = waterLevel;
  publishTopic(PUB_WATER_LEVEL, String(waterLevel));
  Serial.print("Water Level Value: ");
  Serial.println(latestWaterLevel);
  if (latestWaterLevel <= SOLENOID_ON_WATER_LEVEL) {
      digitalWrite(RELAY2_PIN_2, LOW);
      publishTopic(NOTIF_SOLENOID, OPEN);
      Serial.println("Solenoid: OPEN");
  } else if (latestWaterLevel >= SOLENOID_OFF_WATER_LEVEL) {
      digitalWrite(RELAY2_PIN_2, HIGH);
      publishTopic(NOTIF_SOLENOID, CLOSE);
      Serial.println("Solenoid: OFF");
  } else {
      digitalWrite(RELAY2_PIN_2, HIGH);
      publishTopic(NOTIF_SOLENOID, CLOSE);
      Serial.println("Solenoid: OFF");
  }
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

    Serial.print("Light: ");
    Serial.print(lux);
    Serial.println(" lx");
    updateLedState();
  }
}

void tdsMeterSensor() {
  analogBuffer[analogBufferIndex++] = analogRead(TdsSensorPin);
  if (analogBufferIndex == SCOUNT) analogBufferIndex = 0;

  float average = averageAnalogRead();
  float voltage = average * (VREF / 4095.0); // ESP32 ADC is 12-bit
  float tdsValue = (133.42 * voltage * voltage * voltage
                   - 255.86 * voltage * voltage
                   + 857.39 * voltage) * 0.5; // ppm

  latestTdsValue = tdsValue;
  publishTopic(PUB_TDS, String(tdsValue, 2));

  Serial.print("Voltage: ");
  Serial.print(voltage, 2);
  Serial.print(" V  |  TDS: ");
  Serial.print(tdsValue, 0);
  Serial.println(" ppm");
  updatePumpState();
}

const unsigned long SENSOR_INTERVAL = 2000;
unsigned long lastSensorRead = 0;

void loop() {
  if (!client.connected()) {
    reconnect();
  }

  // Process incoming MQTT messages first
  client.loop();

  unsigned long now = millis();

  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;

    // START DHT22 Sensor
    dht22Sensor();
    // END DHT22 Sensor

    // START Capacitive Soil Sensor
    capacitiveSoilSensor();
    // END Capacitive Soil Sensor

    // START Water Level Sensor
    waterLevelSensor();
    // END Water Level Sensor

    // START Light Level Sensor
    lightLevelSensor();
    // END Light Level Sensor

    // START TDS Meter Sensor
    tdsMeterSensor();
    // END TDS Meter Sensor

    Serial.println("");
  }
}

