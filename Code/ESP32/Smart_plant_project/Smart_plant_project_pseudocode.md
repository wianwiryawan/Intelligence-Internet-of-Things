# Pseudocode for Smart Plant Project

## Overview
This program controls an ESP32-based smart plant system that monitors environmental conditions, publishes sensor data to MQTT, receives relay commands, and automatically controls a pump, misting system, LED light, and solenoid valve.

---

## 1. Constant and Threshold Setup

BEGIN
    DEFINE WiFi LED pin = 0
    DEFINE MQTT LED pin = 2

    # Pseudocode for Smart Plant Project

    ## Overview
    The ESP32 reads environmental sensors, publishes measurements and actuator status through MQTT, accepts MQTT commands for four relays, and runs automatic control for the pump, mist generator, LED relay, and solenoid valve. The current sketch has no manual-mode switch; relay commands are accepted whenever their MQTT topics are received.

    ---

    ## 1. Pins and Thresholds

    ```text
    WiFi indicator: GPIO 0
    MQTT indicator: GPIO 2

    Relay outputs (active-low):
        Pump: GPIO 12
        Mist generator: GPIO 14
        LED: GPIO 25
        Solenoid valve: GPIO 26

    DHT22: GPIO 4
        Mist ON at humidity <= 60%
        Mist OFF at humidity > 60% (including the 60%-80% range)
        Temperature thresholds used by LED control: 18 and 27 C

    Capacitive soil sensor: ADC GPIO 35
        Dry-air calibration = 2290
        Submerged calibration = 355

    Water-level sensor: ADC GPIO 34
        Pump water range: 1000 through 2000 ADC counts
        Solenoid opening threshold: <= 1000
        Solenoid closing threshold: >= 1500

    BH1750: SDA GPIO 32, SCL GPIO 33
        LED lux thresholds: 2000 and 5000

    TDS sensor: ADC GPIO 39
        ADC reference = 3.3 V; ADC scale = 4095
        Circular sample buffer size = 30
    ```

    MQTT topics use the `/smartplant/esp32-01/` prefix. Sensor topics are `sensor/temperature`, `sensor/humidity`, `sensor/moisture`, `sensor/water-level`, `sensor/light`, and `sensor/ppm`. Relay command topics are `command/relay/1/1` through `command/relay/2/2`. Status topics are `notification/pump`, `notification/mist`, `notification/led`, and `notification/solenoid`.

    ---

    ## 2. Setup

    ```text
    START serial communication
    Set indicator and relay pins as outputs
    Set all active-low relays HIGH (OFF)
    Connect to WiFi and wait until connected
    Configure MQTT broker and callback
    Initialize DHT22
    Set water-level sensor pin as input
    Initialize I2C and BH1750
        If BH1750 initialization fails, print an error and wait forever
    END setup
    ```

    The MQTT indicator turns HIGH after a successful broker connection and LOW after a failed connection attempt. WiFi connection setup waits indefinitely until WiFi connects.

    ---

    ## 3. MQTT Connection and Commands

    ```text
    FUNCTION reconnectToMQTT()
        WHILE MQTT is disconnected
            Try to connect to the configured broker
            IF connected
                Subscribe to all four relay command topics
                Set MQTT indicator HIGH
            ELSE
                Set MQTT indicator LOW
                Wait 5 seconds
            ENDIF
        ENDWHILE
    END FUNCTION

    FUNCTION onMqttMessage(topic, payload)
        Convert payload bytes to text and print topic and message

        For the relay matching topic:
            IF payload == "1"
                Drive relay pin LOW (ON)
                Publish matching ON status
            ELSE
                Drive relay pin HIGH (OFF)
                Publish matching OFF status
            ENDIF
    END FUNCTION
    ```

    MQTT relay commands are not restricted by an automatic/manual mode. Automatic sensor logic may subsequently change the pump, mist, or LED relay state.

    ---

    ## 4. Sensor Reads

    ```text
    FUNCTION readDhtSensor()
        Read humidity and temperature
        IF either reading is invalid
            Print an error and return
        ENDIF
        Store latest humidity and temperature
        Publish temperature and humidity

        IF humidity <= 60%
            Turn mist relay ON; publish "Aktif"
        ELSE
            Turn mist relay OFF; publish "Mati"
        ENDIF
    END FUNCTION

    FUNCTION readSoilMoistureSensor()
        Read ADC value from GPIO 35
        Map dry-air calibration 2290 to 0%, submerged calibration 355 to 100%
        Constrain result to 0%-100%
        Store and publish soil moisture percentage
    END FUNCTION

    FUNCTION readWaterLevelSensor()
        Read ADC count from GPIO 34
        Store and publish raw count
        IF water level <= 1000
            Turn solenoid relay ON; publish "Open"
        ELSE
            Turn solenoid relay OFF; publish "Close"
        ENDIF
    END FUNCTION

    FUNCTION readLightSensor()
        Read BH1750 lux
        IF lux is invalid
            Print an error
        ELSE
            Store and publish lux
            CALL updateLedState()
        ENDIF
    END FUNCTION

    FUNCTION readTdsSensor()
        Add one ADC reading to the 30-element circular buffer
        Average the buffer, convert ADC average to voltage, then calculate TDS ppm
        Store and publish TDS
        CALL updatePumpState()
    END FUNCTION
    ```

    ---

    ## 5. Automatic Pump Control

    ```text
    FUNCTION updatePumpState()
        IF water level, TDS, or soil moisture is less than 0
            RETURN
        ENDIF

        waterLevelInRange = (1000 <= water level <= 2000)
        tdsInRange = (50 <= TDS <= 300)
        soilInRange = (50 <= soil moisture <= 70)
        soilOver = (soil moisture >= 70)

        IF waterLevelInRange AND tdsInRange
            IF soilInRange OR soilOver
                Turn pump relay OFF; publish "Mati"
            ELSE
                Turn pump relay ON; publish "Aktif"
            ENDIF
        ELSE
            Turn pump relay OFF; publish "Mati"
        ENDIF
    END FUNCTION
    ```

    Because `soilInRange` OR `soilOver` covers every value at or above 50%, the effective soil rule is: pump ON below 50%, OFF at or above 50%. Pump water level and TDS must also be within their inclusive ranges. This function is called after each TDS update.

    ---

    ## 6. Automatic LED Control

    ```text
    FUNCTION updateLedState()
        IF temperature or lux is less than 0
            RETURN
        ENDIF

        temperatureInRange = (18 <= temperature <= 27)
        temperatureOver = (temperature >= 27)
        luxInRange = (2000 <= lux <= 5000)
        luxOver = (lux >= 5000)

        IF temperatureInRange AND luxInRange
            Turn LED relay OFF; publish "Mati"
        ELSE IF luxOver OR temperatureOver
            Turn LED relay OFF; publish "Mati"
        ELSE
            Turn LED relay ON; publish "Aktif"
        ENDIF
    END FUNCTION
    ```

    This function is called after a valid BH1750 reading. Temperature is updated earlier in the same sensor cycle.

    ---

    ## 7. Main Loop

    ```text
    sensorReadInterval = 2000 ms
    lastSensorRead = 0

    LOOP FOREVER
        IF MQTT is disconnected
            reconnectToMQTT()
        ENDIF
        Process incoming MQTT messages

        IF current time - lastSensorRead >= sensorReadInterval
            Update lastSensorRead
            Read DHT22
            Read soil moisture
            Read water level and control solenoid
            Read light and control LED
            Read TDS and control pump
            Print a blank line
        ENDIF
    END LOOP
    ```

    ---

    ## 8. Current Behavior Notes

    - There is no active WS2812B strip setup or update in the current sketch.
    - A separate `updateSolenoidState()` function is defined, but the sensor routine controls the solenoid directly; the function is not called.
    - Relay MQTT commands can be received in any operating state. The current sketch has no manual-mode topic or mode flag.
## 8. Main Loop
