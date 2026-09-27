# Pseudocode for Smart Plant Project

## Overview

The ESP32 reads sensors, publishes readings to MQTT, and accepts MQTT commands for three active-low relays: pump, mist generator, and solenoid valve. Sensor routines automatically control these relays.

---

## 1. Hardware and Thresholds

```text
Status indicators:
    WiFi: GPIO 0
    MQTT: GPIO 2

Active-low relay outputs:
    Pump: GPIO 12
    Mist generator: GPIO 14
    Solenoid valve: GPIO 26

DHT22: GPIO 4
    Mist ON when humidity <= 60%
    Code also checks humidity >= 80% to turn mist OFF
    Temperature constants 18 C and 27 C are not used by active control

Capacitive soil sensor: ADC GPIO 35
    Dry-air reading: 2290
    Fully submerged reading: 355

Water-level sensor: ADC GPIO 34
    Pump allowed water-level range: 1000-2000 ADC counts, inclusive
    Solenoid ON threshold: <= 1000
    Solenoid OFF threshold: >= 1500

BH1750 light sensor: SDA GPIO 32, SCL GPIO 33
TDS sensor: ADC GPIO 39
    Voltage reference: 3.3 V
    ADC scale: 4095
    Average buffer: 30 readings
```

MQTT topics use the `/smartplant/esp32-01/` prefix. The subscribed relay command topics are `/command/relay/1/1` (pump), `/command/relay/1/2` (mist), and `/command/relay/2/2` (solenoid). Sensor readings are published to `/sensor/temperature`, `/sensor/humidity`, `/sensor/moisture`, `/sensor/water-level`, `/sensor/light`, and `/sensor/ppm`. Relay status is published to `/notification/pump`, `/notification/mist`, and `/notification/solenoid`.

---

## 2. Startup and MQTT Connection

```text
Initialize serial communication
Configure WiFi/MQTT indicators and three relay pins as outputs
Set all active-low relays HIGH (OFF)
Connect to WiFi and wait until connected
Choose the configured cloud or local MQTT broker
Configure MQTT server and message callback
Initialize DHT22
Configure water-level sensor pin as input
Initialize I2C and BH1750
    If BH1750 initialization fails, print an error and wait indefinitely

FUNCTION reconnect()
    WHILE MQTT is disconnected
        Try to connect to the broker
        IF connection succeeds
            Subscribe to the three relay command topics
            Set MQTT indicator HIGH
        ELSE
            Set MQTT indicator LOW
            Print connection error and wait 5 seconds
        ENDIF
    ENDWHILE
END FUNCTION
```

WiFi setup waits indefinitely for WiFi. MQTT reconnection blocks in its retry loop until a connection succeeds.

---

## 3. MQTT Relay Command Handler

```text
FUNCTION mqtt_callback(topic, payload)
    Convert payload bytes to a string
    Print the received topic and payload
    Match the topic to the pump, mist, or solenoid relay

    IF payload == "1"
        Drive the matching relay GPIO LOW (ON)
        Publish the matching ON status
    ELSE
        Drive the matching relay GPIO HIGH (OFF)
        Publish the matching OFF status
    ENDIF
END FUNCTION
```

Any payload other than exactly `"1"` turns the selected relay OFF. Commands are accepted without a manual/automatic mode check. Automatic sensor logic may later change relay state.

---

## 4. Sensor Routines

```text
FUNCTION dht22Sensor()
    Read humidity and temperature from DHT22
    IF either reading is invalid
        Print an error and return
    ENDIF
    Store latest humidity and temperature
    Publish temperature and humidity
    Print both values

    IF humidity <= 60%
        Turn mist relay ON; publish "Aktif"
    ELSE IF humidity >= 80%
        Turn mist relay OFF; publish "Mati"
    ELSE
        Turn mist relay OFF; publish "Mati"
    ENDIF
END FUNCTION
```

Effective mist behavior is ON at or below 60% humidity and OFF above 60%; the 80% condition does not provide hysteresis because the final branch also turns it off.

```text
FUNCTION capacitiveSoilSensor()
    Read ADC value from GPIO 35
    Map dry-air calibration 2290 to 0% and submerged calibration 355 to 100%
    Constrain moisture percentage to 0%-100%
    Store and publish moisture percentage
    Print raw reading and converted percentage
END FUNCTION

FUNCTION waterLevelSensor()
    Read ADC count from GPIO 34
    Store and publish the raw count
    Print the water-level count

    IF water level <= 1000
        Turn solenoid relay ON; publish "Open"
    ELSE IF water level >= 1500
        Turn solenoid relay OFF; publish "Close"
    ELSE
        Turn solenoid relay OFF; publish "Close"
    ENDIF
END FUNCTION

FUNCTION lightLevelSensor()
    Read lux from BH1750
    IF lux < 0
        Print an error
    ELSE
        Publish and print lux
    ENDIF
END FUNCTION

FUNCTION tdsMeterSensor()
    Read ADC from GPIO 39 into the next slot of the 30-entry circular buffer
    Average all buffer entries
    voltage = average * (3.3 / 4095)
    tds = (133.42 * voltage^3 - 255.86 * voltage^2 + 857.39 * voltage) * 0.5
    Store and publish TDS
    Print voltage and TDS
    CALL updatePumpState()
END FUNCTION
```

---

## 5. Automatic Pump Control

```text
FUNCTION updatePumpState()
    IF latest water level < 0 OR latest TDS < 0 OR latest soil moisture < 0
        Return without changing pump state
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

Because `soilInRange OR soilOver` covers all soil readings >= 50%, the effective rule is: pump ON below 50% soil moisture, OFF at or above 50%, provided water level and TDS are in their inclusive ranges. The function runs after each TDS reading.

---

## 6. Main Loop

```text
sensorInterval = 2000 ms
lastSensorRead = 0

LOOP FOREVER
    IF MQTT is disconnected
        reconnect()
    ENDIF
    Process incoming MQTT messages

    IF current time - lastSensorRead >= sensorInterval
        lastSensorRead = current time
        Read DHT22 and control mist relay
        Read soil moisture
        Read water level and control solenoid relay
        Read light and publish lux
        Read TDS and update pump relay
        Print a blank line
    ENDIF
END LOOP
```

---
