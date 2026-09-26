# Pseudocode untuk Smart Plant Project

## Gambaran Umum
Program ini mengendalikan sistem tanaman pintar berbasis ESP32 yang memantau kondisi lingkungan, mengirim data sensor ke MQTT, menerima perintah relay, serta mengontrol pompa air, sistem kabut, lampu LED, dan katup solenoid secara otomatis.

---

## 1. Pengaturan Konstanta dan Batas Ambang

BEGIN
    DEFINISIKAN pin LED WiFi = 0
    DEFINISIKAN pin LED MQTT = 2

    DEFINISIKAN pin relay:
        RELAY1_PIN_1 = 12   // pompa air
        RELAY1_PIN_2 = 14   // generator kabut
        RELAY2_PIN_1 = 25   // LED
        RELAY2_PIN_2 = 26   // katup solenoid

    DEFINISIKAN pin sensor DHT = 4
    DEFINISIKAN tipe DHT = DHT22

    DEFINISIKAN rentang suhu ideal = 18 sampai 27 derajat Celsius
    DEFINISIKAN rentang kelembapan ideal untuk misting = 60% sampai 80%

    DEFINISIKAN pin sensor kelembapan tanah = 35
    DEFINISIKAN nilai kalibrasi udara dan air untuk sensor tanah

    DEFINISIKAN pin sensor level air = 34
    DEFINISIKAN rentang level air ideal = 1000 sampai 1500

    DEFINISIKAN sensor cahaya = BH1750 pada SDA=32, SCL=33
    DEFINISIKAN rentang cahaya ideal = 2000 sampai 5000 lux

    DEFINISIKAN pin sensor TDS = 39
    DEFINISIKAN tegangan referensi ADC = 3.3V
    DEFINISIKAN jumlah sampel = 30

    DEFINISIKAN server MQTT dan kredensial WiFi
    DEFINISIKAN topik MQTT untuk data sensor, perintah, dan notifikasi
END

---

## 2. Prosedur Setup

BEGIN
    INISIALISASI komunikasi serial

    SET semua pin relay sebagai OUTPUT
    SET pin indikator WiFi dan MQTT sebagai OUTPUT

    SET semua relay ke keadaan OFF
        // relay bersifat active-low, jadi menulis HIGH akan mematikan relay

    HUBUNGKAN ke WiFi menggunakan SSID dan password yang telah dikonfigurasi
    TUNGGU sampai koneksi WiFi terhubung

    SET alamat dan port broker MQTT
    LAMPIRKAN fungsi callback MQTT untuk menangani pesan masuk

    INISIALISASI sensor DHT22
    INISIALISASI sensor cahaya BH1750
    INISIALISASI pin sensor level air sebagai INPUT
END

---

## 3. Koneksi MQTT dan Langganan

FUNGSI reconnectToMQTT()
    SELAMA MQTT belum terhubung DO
        COBA HUBUNG ke broker MQTT
        JIKA koneksi berhasil MAKA
            LANGGANKAN semua topik kontrol relay
            NYALAKAN LED indikator MQTT
            CETAK "Connected"
        LAINNYA
            MATIKAN LED indikator MQTT
            TUNGGU 5 detik
        ENDIF
    ENDWHILE
END FUNGSI

FUNGSI subscribeTopics()
    LANGGAN topik relay 1/1
    LANGGAN topik relay 1/2
    LANGGAN topik relay 2/1
    LANGGAN topik relay 2/2
END FUNGSI

---

## 4. Handler Pesan MQTT

FUNGSI onMqttMessage(topic, payload)
    UBAH byte payload menjadi string teks
    CETAK topik dan pesan yang diterima

    JIKA topic == SUB_RELAY_1_1 MAKA
        JIKA pesan == "1" MAKA
            NYALAKAN relay pompa air
        LAINNYA
            MATIKAN relay pompa air
        ENDIF

    ELSE IF topic == SUB_RELAY_1_2 MAKA
        JIKA pesan == "1" MAKA
            NYALAKAN relay generator kabut
        LAINNYA
            MATIKAN relay generator kabut
        ENDIF

    ELSE IF topic == SUB_RELAY_2_1 MAKA
        JIKA pesan == "1" MAKA
            NYALAKAN relay LED
        LAINNYA
            MATIKAN relay LED
        ENDIF

    ELSE IF topic == SUB_RELAY_2_2 MAKA
        JIKA pesan == "1" MAKA
            NYALAKAN relay katup solenoid
        LAINNYA
            MATIKAN relay katup solenoid
        ENDIF
    ENDIF
END FUNGSI

---

## 5. Fungsi Pembacaan Sensor

FUNGSI readDhtSensor()
    BACA suhu dan kelembapan dari DHT22

    JIKA pembacaan tidak valid MAKA
        CETAK error
        RETURN
    ENDIF

    SIMPAN nilai suhu dan kelembapan terbaru
    TERBITKAN nilai suhu ke topik MQTT
    TERBITKAN nilai kelembapan ke topik MQTT

    JIKA kelembapan < 60 MAKA
        NYALAKAN relay generator kabut
        TERBITKAN status kabut = "Aktif"
    ELSE IF kelembapan > 80 MAKA
        MATIKAN relay generator kabut
        TERBITKAN status kabut = "Mati"
    ELSE
        MATIKAN relay generator kabut
        TERBITKAN status kabut = "Mati"
    ENDIF
END FUNGSI

FUNGSI readSoilMoistureSensor()
    BACA nilai analog mentah dari sensor tanah
    UBAH nilai mentah menjadi persentase menggunakan rumus kalibrasi
    BATASI nilai antara 0 dan 100

    SIMPAN nilai kelembapan tanah terbaru
    TERBITKAN persentase kelembapan ke MQTT
END FUNGSI

FUNGSI readWaterLevelSensor()
    BACA nilai analog dari sensor level air
    SIMPAN nilai level air terbaru
    TERBITKAN level air ke MQTT
END FUNGSI

FUNGSI readLightSensor()
    BACA level cahaya dalam lux menggunakan BH1750

    JIKA pembacaan cahaya valid MAKA
        SIMPAN nilai lux terbaru
        TERBITKAN nilai lux ke MQTT
        PANGGIL updateLedState()
    ENDIF
END FUNGSI

FUNGSI readTdsSensor()
    BACA nilai analog dari sensor TDS
    SIMPAN sampel ke buffer melingkar
    HITUNG nilai rata-rata
    UBAH pembacaan ADC rata-rata menjadi tegangan
    HITUNG TDS dalam ppm menggunakan rumus

    SIMPAN nilai TDS terbaru
    TERBITKAN nilai TDS ke MQTT
    PANGGIL updatePumpState()
END FUNGSI

---

## 6. Logika Kendali Otomatis untuk Pompa

FUNGSI updatePumpState()
    JIKA nilai yang dibutuhkan belum tersedia MAKA
        RETURN
    ENDIF

    waterLevelInRange = (levelAir > 1000 DAN levelAir < 1500)
    waterLevelOver = (levelAir > 1500)

    tdsInRange = (tds > 50 DAN tds < 300)
    tdsOver = (tds > 300)

    soilInRange = (kelembapanTanah > 50 DAN kelembapanTanah < 70)
    soilOver = (kelembapanTanah > 70)

    JIKA waterLevelInRange DAN tdsInRange DAN soilInRange MAKA
        MATIKAN pompa air
        TERBITKAN status pompa = "Mati"
    ELSE IF waterLevelOver ATAU tdsOver ATAU soilOver MAKA
        MATIKAN pompa air
        TERBITKAN status pompa = "Mati"
    ELSE
        NYALAKAN pompa air
        TERBITKAN status pompa = "Aktif"
    ENDIF
END FUNGSI

---

## 7. Logika Kendali Otomatis untuk LED

FUNGSI updateLedState()
    JIKA nilai suhu atau lux belum tersedia MAKA
        RETURN
    ENDIF

    temperatureInRange = (suhu > 18 DAN suhu < 27)
    temperatureOver = (suhu > 27)

    luxInRange = (lux > 2000 DAN lux < 5000)
    luxOver = (lux > 5000)

    JIKA temperatureInRange DAN luxInRange MAKA
        MATIKAN LED
        TERBITKAN status LED = "Mati"
    ELSE IF luxOver ATAU temperatureOver MAKA
        MATIKAN LED
        TERBITKAN status LED = "Mati"
    ELSE
        NYALAKAN LED
        TERBITKAN status LED = "Aktif"
    ENDIF
END FUNGSI

---

## 8. Loop Utama

SET intervalPembacaanSensor = 2000 ms
SET lastSensorRead = 0

LOOP FOREVER
    JIKA MQTT terputus MAKA
        reconnectToMQTT()
    ENDIF

    PROSES pesan MQTT yang masuk

    currentTime = millis()

    JIKA currentTime - lastSensorRead >= intervalPembacaanSensor MAKA
        lastSensorRead = currentTime

        PANGGIL readDhtSensor()
        PANGGIL readSoilMoistureSensor()
        PANGGIL readWaterLevelSensor()
        PANGGIL readLightSensor()
        PANGGIL readTdsSensor()

        CETAK baris kosong
    ENDIF
END LOOP

---

## 9. Ringkasan Perilaku

Sistem ini melakukan hal berikut:
- Mengukur suhu, kelembapan, kelembapan tanah, level air, intensitas cahaya, dan TDS.
- Menerbitkan nilai sensor ke topik MQTT.
- Menerima perintah relay manual dari MQTT.
- Mengendalikan pompa air secara otomatis berdasarkan level air, TDS, dan kelembapan tanah.
- Mengendalikan lampu LED secara otomatis berdasarkan suhu dan tingkat cahaya.
- Mengendalikan misting berdasarkan kelembapan udara.
- Menerbitkan notifikasi status seperti "Aktif" dan "Mati".
