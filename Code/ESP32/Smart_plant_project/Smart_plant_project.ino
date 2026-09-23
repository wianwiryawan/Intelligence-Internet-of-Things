#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <BH1750.h>

// LED WiFi indicator
#define LED_PIN_WIFI 0

// LED MQTT indicator
#define LED_PIN_MQTT 2

// 2 Way Module Relay HW-383
#define RELAY1_PIN_1 12
#define RELAY1_PIN_2 14

// DHT Sensor
#define DHTPIN 4        // Digital pin connected to the DHT sensor
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);

// Capacitive Soil Moisture Sensor
const int sensorInPin = 35;

// Calibration values (Update these based on your own sensor testing)
const int AirValue = 3500;   // Sensor reading in dry air
const int WaterValue = 1500; // Sensor reading completely submerged in water

// MH Water Sensor (water level)
#define WATER_SENSOR 34

// Light Sensor
BH1750 lightMeter;
#define LIGHT_SDA 32
#define LIGHT_SCL 33

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

const char* ssid = "LaptopLitya";
const char* password = "binusplenger";

// MQTT Server
const bool isCloud = false;
const char* cloudMqttServer = "broker.hivemq.com"; // MQTT cloud broker
const int cloudMqttPort = 1883; // MQTT cloud port 
const char* localMqttServer = "192.168.137.60"; // MQTT local broker
const int localMqttPort = 1883; // MQTT local port

WiFiClient espClient;
PubSubClient client(espClient);

// MQTT Topics
const char* TOPIC_RELAY_1_X1   = "/RELAY_1/X1/V1";
const char* TOPIC_RELAY_1_X2   = "/RELAY_1/X2/V1";
const char* TOPIC_TEMPERATURE  = "/TEMPERATURE/V1";
const char* TOPIC_HUMIDITY     = "/HUMIDITY/V1";
const char* TOPIC_MOISTURE     = "/MOISTURE/V1";
const char* TOPIC_WATER_LEVEL  = "/WATER_LEVEL/V1";
const char* TOPIC_LIGHT        = "/LIGHT/V1";
const char* TOPIC_TDS          = "/PPM/V1";

const unsigned long SENSOR_INTERVAL = 5000;
unsigned long lastSensorRead = 0;

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
  Serial.print(string);
  Serial.println("");

  if (String(topic) == TOPIC_RELAY_1_X1) {
    if (string == "1") {
      digitalWrite(RELAY1_PIN_1, LOW);
    } else {
      digitalWrite(RELAY1_PIN_1, HIGH);
    }
  } else if (String(topic) == TOPIC_RELAY_1_X2) {
    if (string == "1") {
      digitalWrite(RELAY1_PIN_2, LOW);
    } else {
      digitalWrite(RELAY1_PIN_2, HIGH);
    }
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(LED_PIN_WIFI, OUTPUT); // WiFi indicator LED
  pinMode(LED_PIN_MQTT, OUTPUT); // MQTT indicator LED

  pinMode(RELAY1_PIN_1, OUTPUT); // Relay 1 X1
  pinMode(RELAY1_PIN_2, OUTPUT); // Relay 1 X2

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
      Serial.println("connected");
      
      bool sub1 = client.subscribe(TOPIC_RELAY_1_X1);
      bool sub2 = client.subscribe(TOPIC_RELAY_1_X2);

      digitalWrite(LED_PIN_MQTT, HIGH);

      Serial.print("Subscribe relay 1: ");
      Serial.println(sub1 ? "OK" : "FAILED");

      Serial.print("Subscribe relay 2: ");
      Serial.println(sub2 ? "OK" : "FAILED");
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
  
  publishTopic(TOPIC_TEMPERATURE, String(t, 2));
  publishTopic(TOPIC_HUMIDITY, String(h, 2));

  Serial.print(F("Humidity: "));
  Serial.print(h);
  Serial.print(F("%  Temperature: "));
  Serial.print(t);
  Serial.println(F("°C"));
}

void capacitiveSoilSensor() {
  int sensorVal = analogRead(sensorInPin); // Read analog value from ESP32 pin
  
  // Convert raw reading to a percentage (constrained between 0% and 100%)
  int moisturePercent = map(sensorVal, AirValue, WaterValue, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  publishTopic(TOPIC_MOISTURE, String(moisturePercent));

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

  publishTopic(TOPIC_WATER_LEVEL, String(waterLevel));
  Serial.print("Water Level Value: ");
  Serial.println(waterLevel);
}

void lightLevelSensor() {
  // Read light level in lux
  float lux = lightMeter.readLightLevel();

  // Validate reading
  if (lux < 0) {
    Serial.println("Error reading light level.");
  } else {
    publishTopic(TOPIC_LIGHT, String(lux, 2));

    Serial.print("Light: ");
    Serial.print(lux);
    Serial.println(" lx");
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

  publishTopic(TOPIC_TDS, String(tdsValue, 2));

  Serial.print("Voltage: ");
  Serial.print(voltage, 2);
  Serial.print(" V  |  TDS: ");
  Serial.print(tdsValue, 0);
  Serial.println(" ppm");
}

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

