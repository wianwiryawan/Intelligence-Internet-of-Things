#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHTesp.h>

// ================= WIFI =================
const char* WIFI_SSID = "wiyan";
const char* WIFI_PASSWORD = "12345678";

// ================= THINGSBOARD =================
const char* MQTT_SERVER = "eu.thingsboard.cloud";
const int MQTT_PORT = 1883;

const char* TOKEN = "hwcyh7fuy7jdylrupd6u";

// ================= DHT =================
DHTesp dhtSensor;

// ================= MQTT =================
WiFiClient espClient;
PubSubClient client(espClient);

// ================= ML MODEL =================
float w1 = -0.4234;
float w2 = -0.0383;
float w3 = -0.0220;
float w4 = -0.0198;
float w5 = 0.2154;
float b  = 33.0164;

// ================= WINDOW =================
const int WINDOW_SIZE = 5;
float tempWindow[WINDOW_SIZE] = {0,0,0,0,0};

int tempCount = 0;

// ================= TIMING =================
unsigned long lastTelemetry = 0;
unsigned long lastAttrRequest = 0;

const long TELEMETRY_INTERVAL = 5000;
const long ATTRIBUTE_INTERVAL = 30000;

// =================================================
// MQTT CALLBACK
// =================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {

  Serial.println("\n===== ATTRIBUTE UPDATE =====");

  String json = "";

  for (int i = 0; i < length; i++) {
    json += (char) payload[i];
  }

  Serial.println(json);

  DynamicJsonDocument doc(1024);

  deserializeJson(doc, json);

  JsonObject shared = doc["shared"];

  if (!shared.isNull()) {

    if (shared.containsKey("w1")) w1 = shared["w1"];
    if (shared.containsKey("w2")) w2 = shared["w2"];
    if (shared.containsKey("w3")) w3 = shared["w3"];
    if (shared.containsKey("w4")) w4 = shared["w4"];
    if (shared.containsKey("w5")) w5 = shared["w5"];
    if (shared.containsKey("b"))  b  = shared["b"];
  }

  Serial.print("w1 = "); Serial.println(w1);
  Serial.print("w2 = "); Serial.println(w2);
  Serial.print("w3 = "); Serial.println(w3);
  Serial.print("w4 = "); Serial.println(w4);
  Serial.print("w5 = "); Serial.println(w5);
  Serial.print("b  = "); Serial.println(b);

  Serial.println("============================");
}

// =================================================
// WIFI
// =================================================
void connectWiFi() {

  Serial.println("Connecting WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());
}

// =================================================
// MQTT CONNECT
// =================================================
void connectMQTT() {

  while (!client.connected()) {

    Serial.println("Connecting ThingsBoard MQTT...");

    if (client.connect("ESP32", TOKEN, NULL)) {

      Serial.println("MQTT Connected");

      // subscribe attributes
      client.subscribe("v1/devices/me/attributes");

      // request shared attributes
      client.publish(
        "v1/devices/me/attributes/request/1",
        "{\"sharedKeys\":\"w1,w2,w3,w4,w5,b\"}"
      );

    } else {

      Serial.print("Failed MQTT, rc=");
      Serial.println(client.state());

      delay(2000);
    }
  }
}

// =================================================
// SHIFT WINDOW
// =================================================
void shiftWindow(float value) {

  for (int i = 0; i < WINDOW_SIZE - 1; i++) {
    tempWindow[i] = tempWindow[i + 1];
  }

  tempWindow[WINDOW_SIZE - 1] = value;
}

// =================================================
// ML PREDICTION
// =================================================
float predictTemperature() {

  return
    (w1 * tempWindow[0]) +
    (w2 * tempWindow[1]) +
    (w3 * tempWindow[2]) +
    (w4 * tempWindow[3]) +
    (w5 * tempWindow[4]) +
    b;
}

// =================================================
// PRINT WINDOW
// =================================================
void printWindow() {

  Serial.print("Window: ");

  for (int i = 0; i < WINDOW_SIZE; i++) {

    Serial.print(tempWindow[i], 2);

    if (i < WINDOW_SIZE - 1)
      Serial.print(", ");
  }

  Serial.println();
}

// =================================================
// SETUP
// =================================================
void setup() {

  Serial.begin(115200);

  dhtSensor.setup(4, DHTesp::DHT22);

  connectWiFi();

  client.setServer(MQTT_SERVER, MQTT_PORT);

  client.setCallback(mqttCallback);
}

// =================================================
// LOOP
// =================================================
void loop() {

  if (!client.connected()) {
    connectMQTT();
  }

  client.loop();

  // ===== REQUEST ATTRIBUTES PERIODIC =====
  if (millis() - lastAttrRequest > ATTRIBUTE_INTERVAL) {

    lastAttrRequest = millis();

    client.publish(
      "v1/devices/me/attributes/request/1",
      "{\"sharedKeys\":\"w1,w2,w3,w4,w5,b\"}"
    );

    Serial.println("Requesting latest ML model...");
  }

  // ===== TELEMETRY =====
  if (millis() - lastTelemetry < TELEMETRY_INTERVAL)
    return;

  lastTelemetry = millis();

  TempAndHumidity data = dhtSensor.getTempAndHumidity();

  float temperature = data.temperature;
  float humidity = data.humidity;

  if (isnan(temperature) || isnan(humidity)) {

    Serial.println("DHT Error");
    return;
  }

  // ===== INITIAL FILL =====
  if (tempCount < WINDOW_SIZE) {

    tempWindow[tempCount] = temperature;
    tempCount++;

    Serial.print("Collecting: ");
    Serial.println(temperature);

    return;
  }

  // ===== PREDICTION =====
  float prediction = predictTemperature();

  // ===== UPDATE WINDOW =====
  shiftWindow(temperature);

  // ===== ANOMALY =====
  bool anomaly = false;

  if (abs(temperature - prediction) > 3.0) {
    anomaly = true;
  }

  // ===== SERIAL =====
  Serial.println("\n===== ML PREDICTION =====");

  printWindow();

  Serial.print("Current Temp: ");
  Serial.println(temperature);

  Serial.print("Prediction: ");
  Serial.println(prediction);

  Serial.print("Anomaly: ");
  Serial.println(anomaly);

  // ===== JSON TELEMETRY =====
  DynamicJsonDocument doc(512);

  doc["temperature"] = temperature;
  doc["humidity"] = humidity;

  doc["prediction"] = prediction;

  doc["anomaly"] = anomaly;

  doc["w1"] = w1;
  doc["w2"] = w2;
  doc["w3"] = w3;
  doc["w4"] = w4;
  doc["w5"] = w5;
  doc["b"] = b;

  String payload;

  serializeJson(doc, payload);

  client.publish(
    "v1/devices/me/telemetry",
    payload.c_str()
  );

  Serial.println("Telemetry Sent");
}
