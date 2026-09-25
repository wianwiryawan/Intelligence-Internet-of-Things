#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHTesp.h>

// ===== WIFI =====
const char* WIFI_SSID = "wiyan";
const char* WIFI_PASSWORD = "12345678";

// ===== THINGSBOARD =====
const char* MQTT_SERVER = "eu.thingsboard.cloud";
const int MQTT_PORT = 1883;
const char* TOKEN = "hwcyh7fuy7jdylrupd6u";

// ===== DHT =====
#define DHT_PIN 4
DHTesp dhtSensor;

// ===== MQTT =====
WiFiClient espClient;
PubSubClient client(espClient);

// ===== ML MODEL =====
// score = w1*temperature + w2*humidity + b
float w1 = 0.65;   // temperature weight
float w2 = 0.25;   // humidity weight
float b  = -5.00;  // bias

unsigned long lastSend = 0;
const long SEND_INTERVAL = 5000;

// ===== WIFI CONNECT =====
void connectWiFi() {
  Serial.print("Connecting WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
  Serial.println(WiFi.localIP());
}

// ===== MQTT CONNECT =====
void connectMQTT() {
  while (!client.connected()) {
    Serial.println("Connecting ThingsBoard...");

    if (client.connect("ESP32_ML_Classifier", TOKEN, NULL)) {
      Serial.println("Connected to ThingsBoard");
    } else {
      Serial.print("Failed, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

// ===== ML SCORE =====
float calculateScore(float temperature, float humidity) {
  return (w1 * temperature) + (w2 * humidity) + b;
}

// ===== CLASSIFICATION =====
int classify(float score) {
  if (score >= 40) {
    return 2; // DANGER
  } else if (score >= 30) {
    return 1; // WARNING
  } else {
    return 0; // NORMAL
  }
}

// ===== LABEL =====
String getLabel(int classId) {
  if (classId == 0) return "NORMAL";
  if (classId == 1) return "WARNING";
  if (classId == 2) return "DANGER";
  return "UNKNOWN";
}

void setup() {
  Serial.begin(115200);

  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);

  connectWiFi();

  client.setServer(MQTT_SERVER, MQTT_PORT);
}

void loop() {
  if (!client.connected()) {
    connectMQTT();
  }

  client.loop();

  if (millis() - lastSend < SEND_INTERVAL) {
    return;
  }

  lastSend = millis();

  TempAndHumidity data = dhtSensor.getTempAndHumidity();

  float temperature = data.temperature;
  float humidity = data.humidity;

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("DHT22 read failed");
    return;
  }

  // ===== ML CLASSIFICATION =====
  float score = calculateScore(temperature, humidity);
  int classId = classify(score);
  String classLabel = getLabel(classId);

  Serial.println("===== ML CLASSIFICATION =====");
  Serial.print("Temperature: ");
  Serial.println(temperature);

  Serial.print("Humidity: ");
  Serial.println(humidity);

  Serial.print("Score: ");
  Serial.println(score);

  Serial.print("Class: ");
  Serial.println(classLabel);

  // ===== JSON TELEMETRY =====
  DynamicJsonDocument doc(512);

  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["ml_score"] = score;
  doc["class_id"] = classId;
  doc["class_label"] = classLabel;

  // Indicator values for ThingsBoard dashboard
  doc["normal"] = classId == 0 ? 1 : 0;
  doc["warning"] = classId == 1 ? 1 : 0;
  doc["danger"] = classId == 2 ? 1 : 0;

  String payload;
  serializeJson(doc, payload);

  client.publish("v1/devices/me/telemetry", payload.c_str());

  Serial.println("Telemetry sent:");
  Serial.println(payload);
}