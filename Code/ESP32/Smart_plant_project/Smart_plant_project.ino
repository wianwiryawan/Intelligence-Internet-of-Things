#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>

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

// Water Level MH Sensor
#define WATER_SENSOR_PIN 34

const char* ssid = "your-ssid";
const char* password = "your-password";
// const char* mqtt_cloud_server = "broker.hivemq.com";// MQTT cloud broker
// const int localMqttPort = 1883; // MQTT cloud broker port
const char* localMqttServer = "your-local-mqtt-host";
const int localMqttPort = 1883; // Your local MQTT port

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
    delay(500);
    Serial.print(".");
    Serial.print(" status=");
    Serial.println(WiFi.status());
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(9600);
  
  // START WiFi Setup
  setup_wifi();
  client.setServer(localMqttServer, localMqttPort);
  // END WiFi Setup

  dht.begin();

  pinMode(WATER_SENSOR_PIN, INPUT);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESPClient")) {
      Serial.println("connected");
      // client.subscribe("/EME/LED1/WIYAN");
      // client.subscribe("/EME/LED2/WIYAN");
    } else {
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
  String publishTopicTemperature = "/EME/DHT1/TEMPERATURE/WIYAN";
  String publishTopicHumidity = "/EME/DHT1/HUMIDITY/WIYAN";


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
  int waterLevel = analogRead(WATER_SENSOR_PIN);
  Serial.print("Water Level Value: ");
  Serial.println(waterLevel);
  // END Water Level Sensor
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

  // Wait 2 seconds between measurements
  delay(2000);
}

