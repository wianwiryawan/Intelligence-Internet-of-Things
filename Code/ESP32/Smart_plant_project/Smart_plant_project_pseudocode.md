# Pseudocode for Smart Plant Project

## Overview
This program controls an ESP32-based smart plant system that monitors environmental conditions, publishes sensor data to MQTT, receives relay commands, and automatically controls a pump, misting system, LED light, and solenoid valve.

---

## 1. Constant and Threshold Setup

BEGIN
    DEFINE WiFi LED pin = 0
    DEFINE MQTT LED pin = 2

    DEFINE relay pins:
        RELAY1_PIN_1 = 12   // water pump
        RELAY1_PIN_2 = 14   // mist generator
        RELAY2_PIN_1 = 25   // LED
        RELAY2_PIN_2 = 26   // solenoid valve

    DEFINE DHT sensor pin = 4
    DEFINE DHT type = DHT22

    DEFINE ideal temperature range = 18 to 27 Celsius
    DEFINE ideal humidity range for misting = 60% to 80%

    DEFINE soil moisture sensor pin = 35
    DEFINE air and water calibration values for soil sensor

    DEFINE water level sensor pin = 34
    DEFINE ideal water level range = 1000 to 1500

    DEFINE lux sensor = BH1750 on SDA=32, SCL=33
    DEFINE ideal light range = 2000 to 5000 lux

    DEFINE TDS sensor pin = 39
    DEFINE ADC reference voltage = 3.3V
    DEFINE sample count = 30

    DEFINE MQTT server and WiFi credentials
    DEFINE MQTT topics for sensor data, commands, and notifications
END

---

## 2. Setup Procedure

BEGIN
    INITIALIZE serial communication

    SET all relay pins as OUTPUT
    SET WiFi and MQTT indicator pins as OUTPUT

    SET all relays to OFF state
        // relay is active-low, so writing HIGH turns it OFF

    CONNECT to WiFi using configured SSID and password
    WAIT until WiFi connection is established

    SET MQTT broker address and port
    ATTACH MQTT callback function to handle incoming messages

    INITIALIZE DHT22 sensor
    INITIALIZE BH1750 light sensor
    INITIALIZE water level sensor pin as INPUT
END

---

## 3. MQTT Connection and Subscription

FUNCTION reconnectToMQTT()
    WHILE MQTT is not connected DO
        TRY TO CONNECT to MQTT broker
        IF connection succeeds THEN
            SUBSCRIBE to all relay control topics
            TURN ON MQTT indicator LED
            PRINT "Connected"
        ELSE
            TURN OFF MQTT indicator LED
            WAIT 5 seconds
        ENDIF
    ENDWHILE
END FUNCTION

FUNCTION subscribeTopics()
    SUBSCRIBE to relay topic 1/1
    SUBSCRIBE to relay topic 1/2
    SUBSCRIBE to relay topic 2/1
    SUBSCRIBE to relay topic 2/2
END FUNCTION

---

## 4. MQTT Message Handler

FUNCTION onMqttMessage(topic, payload)
    CONVERT payload bytes to text string
    PRINT received topic and message

    IF topic == SUB_RELAY_1_1 THEN
        IF message == "1" THEN
            TURN water pump relay ON
        ELSE
            TURN water pump relay OFF
        ENDIF

    ELSE IF topic == SUB_RELAY_1_2 THEN
        IF message == "1" THEN
            TURN mist generator relay ON
        ELSE
            TURN mist generator relay OFF
        ENDIF

    ELSE IF topic == SUB_RELAY_2_1 THEN
        IF message == "1" THEN
            TURN LED relay ON
        ELSE
            TURN LED relay OFF
        ENDIF

    ELSE IF topic == SUB_RELAY_2_2 THEN
        IF message == "1" THEN
            TURN solenoid valve relay ON
        ELSE
            TURN solenoid valve relay OFF
        ENDIF
    ENDIF
END FUNCTION

---

## 5. Sensor Reading Functions

FUNCTION readDhtSensor()
    READ temperature and humidity from DHT22

    IF reading is invalid THEN
        PRINT error
        RETURN
    ENDIF

    STORE latest temperature and humidity values
    PUBLISH temperature value to MQTT topic
    PUBLISH humidity value to MQTT topic

    IF humidity < 60 THEN
        TURN mist generator relay ON
        PUBLISH mist status = "Aktif"
    ELSE IF humidity > 80 THEN
        TURN mist generator relay OFF
        PUBLISH mist status = "Mati"
    ELSE
        TURN mist generator relay OFF
        PUBLISH mist status = "Mati"
    ENDIF
END FUNCTION

FUNCTION readSoilMoistureSensor()
    READ raw analog value from soil sensor
    CONVERT raw value to percentage using calibration formula
    LIMIT value between 0 and 100

    STORE latest soil moisture value
    PUBLISH moisture percentage to MQTT
END FUNCTION

FUNCTION readWaterLevelSensor()
    READ analog value from water level sensor
    STORE latest water level value
    PUBLISH water level to MQTT
END FUNCTION

FUNCTION readLightSensor()
    READ light level in lux using BH1750

    IF light reading is valid THEN
        STORE latest lux value
        PUBLISH lux value to MQTT
        CALL updateLedState()
    ENDIF
END FUNCTION

FUNCTION readTdsSensor()
    READ analog value from TDS sensor
    STORE sample into circular buffer
    CALCULATE average value
    CONVERT average ADC reading to voltage
    CALCULATE TDS in ppm using formula

    STORE latest TDS value
    PUBLISH TDS value to MQTT
    CALL updatePumpState()
END FUNCTION

---

## 6. Automatic Control Logic for Pump

FUNCTION updatePumpState()
    IF any required values are not available THEN
        RETURN
    ENDIF

    waterLevelInRange = (waterLevel > 1000 AND waterLevel < 1500)
    waterLevelOver = (waterLevel > 1500)

    tdsInRange = (tds > 50 AND tds < 300)
    tdsOver = (tds > 300)

    soilInRange = (soilMoisture > 50 AND soilMoisture < 70)
    soilOver = (soilMoisture > 70)

    IF waterLevelInRange AND tdsInRange AND soilInRange THEN
        TURN water pump OFF
        PUBLISH pump status = "Mati"
    ELSE IF waterLevelOver OR tdsOver OR soilOver THEN
        TURN water pump OFF
        PUBLISH pump status = "Mati"
    ELSE
        TURN water pump ON
        PUBLISH pump status = "Aktif"
    ENDIF
END FUNCTION

---

## 7. Automatic Control Logic for LED

FUNCTION updateLedState()
    IF temperature or lux values are not available THEN
        RETURN
    ENDIF

    temperatureInRange = (temperature > 18 AND temperature < 27)
    temperatureOver = (temperature > 27)

    luxInRange = (lux > 2000 AND lux < 5000)
    luxOver = (lux > 5000)

    IF temperatureInRange AND luxInRange THEN
        TURN LED OFF
        PUBLISH LED status = "Mati"
    ELSE IF luxOver OR temperatureOver THEN
        TURN LED OFF
        PUBLISH LED status = "Mati"
    ELSE
        TURN LED ON
        PUBLISH LED status = "Aktif"
    ENDIF
END FUNCTION

---

## 8. Main Loop

SET sensorReadInterval = 2000 ms
SET lastSensorRead = 0

LOOP FOREVER
    IF MQTT is disconnected THEN
        reconnectToMQTT()
    ENDIF

    PROCESS incoming MQTT messages

    currentTime = millis()

    IF currentTime - lastSensorRead >= sensorReadInterval THEN
        lastSensorRead = currentTime

        CALL readDhtSensor()
        CALL readSoilMoistureSensor()
        CALL readWaterLevelSensor()
        CALL readLightSensor()
        CALL readTdsSensor()

        PRINT blank line
    ENDIF
END LOOP

---

## 9. Summary of Behavior

The system does the following:
- Measures temperature, humidity, soil moisture, water level, light intensity, and TDS.
- Publishes sensor values to MQTT topics.
- Accepts manual relay commands from MQTT.
- Automatically controls the water pump based on water level, TDS, and soil moisture.
- Automatically controls the LED light based on temperature and light level.
- Controls misting according to air humidity.
- Publishes status notifications such as "Aktif" and "Mati".
