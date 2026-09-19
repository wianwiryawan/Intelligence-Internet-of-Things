#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <BH1750.h>

// LED WiFi indicator
#define LED_PIN_WIFI 0

// LED MQTT indicator
#define LED_PIN_MQTT 2

// DHT Sensor
#define DHTPIN 4     // Digital pin connected to the DHT sensor
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);

// Define the analog pin connected to the sensor
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
unsigned long lastMsg = 0;


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

void setup() {
  Serial.begin(9600);

  pinMode(LED_PIN_WIFI, OUTPUT); // WiFi indicator LED
  pinMode(LED_PIN_MQTT, OUTPUT); // MQTT indicator LED

  // START WiFi Setup
  setup_wifi();
  // END WiFi Setup

  // START MQTT Setup
  if(isCloud){
    client.setServer(cloudMqttServer, cloudMqttPort);
  } else {
    client.setServer(localMqttServer, localMqttPort);
  }
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
      digitalWrite(LED_PIN_MQTT, HIGH); // LED ON
      // client.subscribe("/EME/LED1/WIYAN");
      // client.subscribe("/EME/LED2/WIYAN");
    } else {
      digitalWrite(LED_PIN_MQTT, LOW); // LED OFF
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
  const char* publishTopicTemperature = "/EME/DHT1/TEMPERATURE/WIYAN";
  const char* publishTopicHumidity = "/EME/DHT1/HUMIDITY/WIYAN";

  // Check if readings failed
  if (isnan(h) || isnan(t)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  unsigned long now = millis();
  if (now - lastMsg > 2000) {
    lastMsg = now;
    
    // Convert float to String
    String temperature = String(t, 2);
    String humidity = String(h, 2);

    client.publish(publishTopicTemperature, temperature.c_str()); 
    client.publish(publishTopicHumidity, humidity.c_str()); 

    Serial.print(F("Humidity: "));
    Serial.print(h);
    Serial.print(F("%  Temperature: "));
    Serial.print(t);
    Serial.println(F("°C"));
  }
}

void capacitiveSoilSensor() {
  int sensorVal = analogRead(sensorInPin); // Read analog value from ESP32 pin
  
  // Convert raw reading to a percentage (constrained between 0% and 100%)
  int moisturePercent = map(sensorVal, AirValue, WaterValue, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  Serial.print("Raw Value: ");
  Serial.print(sensorVal);
  Serial.print(" | Moisture: ");
  Serial.print(moisturePercent);
  Serial.println("%");
}

void waterLevelSensor() {
  // START Water Level Sensor
  int waterLevel = analogRead(WATER_SENSOR);
  Serial.print("Water Level Value: ");
  Serial.println(waterLevel);
  // END Water Level Sensor
}

void lightLevelSensor() {
  // Read light level in lux
  float lux = lightMeter.readLightLevel();

  // Validate reading
  if (lux < 0) {
    Serial.println("Error reading light level.");
  } else {
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

  // Wait 5 seconds between measurements
  delay(5000);
}

