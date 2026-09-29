
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <DHTesp.h>

DHTesp dhtSensor;
TempAndHumidity data;
const int DHT_PIN=4;


// WiFi Connection
const char* WIFI_SSID = "Kantin Belakang";
const char* WIFI_PASSWORD = "PemudaTersesat27";
// 
#define BOTtoken "8639341312:AAG372LzCgGKNlyYcty0LN_v_2owN3V3-Tw"  
//#define BOTtoken "950788494:AAH4VZFifS0oPGbhsIT0tiirSjEIgSlV1y0" 
// your Bot Token (Get from Botfather)
#define CHAT_ID "1867180594"
#define CHAT_ID2 "xxxxxx"

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);


// Checks for new messages every 1 second.
//int botRequestDelay = 1000;
//unsigned long lastTimeBotRan;


String getReadings(){
    data=dhtSensor.getTempAndHumidity();
  String message = "Suhu: " + String(data.temperature) + " ºC \n";
  message += "Kelembapan: " + String (data.humidity) + " % \n";
  return message;

}



void setup(void) {
  Serial.begin(115200);

  dhtSensor.setup(DHT_PIN,DHTesp::DHT22);

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ");
  Serial.print(WIFI_SSID);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }
  Serial.println(" Connected!");

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  #ifdef ESP32
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org
  #endif
 
  // Print ESP32 Local IP Address
  Serial.println(WiFi.localIP());

}

void loop()
{
String readings = getReadings();
      bot.sendMessage(CHAT_ID2, readings, "");
      bot.sendMessage(CHAT_ID, readings, "");
      delay(6000); // every 5 seconds sends

}