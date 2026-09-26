# Pseudocode untuk Smart Plant Project

## Gambaran Umum
ESP32 membaca sensor lingkungan, menerbitkan pengukuran dan status aktuator melalui MQTT, menerima perintah MQTT untuk empat relay, serta menjalankan kendali otomatis untuk pompa, generator kabut, relay LED, dan katup solenoid. Sketch saat ini tidak memiliki sakelar mode manual; perintah relay diterima setiap kali topik MQTT terkait diterima.

---

## 1. Pin dan Ambang Batas

```text
Indikator WiFi: GPIO 0
Indikator MQTT: GPIO 2

Keluaran relay (aktif-low):
    Pompa: GPIO 12
    Generator kabut: GPIO 14
    LED: GPIO 25
    Katup solenoid: GPIO 26

DHT22: GPIO 4
    Kabut ON saat kelembapan <= 60%
    Kabut OFF saat kelembapan > 60% (termasuk rentang 60%-80%)
    Ambang suhu untuk kendali LED: 18 dan 27 C

Sensor kelembapan tanah kapasitif: ADC GPIO 35
    Kalibrasi udara kering = 2290
    Kalibrasi terendam air = 355

Sensor level air: ADC GPIO 34
    Rentang air untuk pompa: 1000 sampai 2000 hitungan ADC
    Ambang buka solenoid: <= 1000
    Ambang tutup solenoid: >= 1500

BH1750: SDA GPIO 32, SCL GPIO 33
    Ambang lux untuk LED: 2000 dan 5000

Sensor TDS: ADC GPIO 39
    Referensi ADC = 3.3 V; skala ADC = 4095
    Ukuran buffer sampel melingkar = 30
```

Topik MQTT menggunakan prefiks `/smartplant/esp32-01/`. Topik sensor adalah `sensor/temperature`, `sensor/humidity`, `sensor/moisture`, `sensor/water-level`, `sensor/light`, dan `sensor/ppm`. Topik perintah relay adalah `command/relay/1/1` sampai `command/relay/2/2`. Topik status adalah `notification/pump`, `notification/mist`, `notification/led`, dan `notification/solenoid`.

---

## 2. Setup

```text
MULAI komunikasi serial
Atur pin indikator dan relay sebagai OUTPUT
Atur semua relay aktif-low ke HIGH (OFF)
Hubungkan ke WiFi dan tunggu sampai tersambung
Atur broker MQTT dan callback
Inisialisasi DHT22
Atur pin sensor level air sebagai INPUT
Inisialisasi I2C dan BH1750
    Jika inisialisasi BH1750 gagal, cetak error lalu tunggu tanpa batas
SELESAI setup
```

Indikator MQTT menjadi HIGH setelah koneksi broker berhasil dan LOW setelah percobaan koneksi gagal. Setup WiFi menunggu tanpa batas sampai WiFi tersambung.

---

## 3. Koneksi dan Perintah MQTT

```text
FUNGSI reconnectToMQTT()
    SELAMA MQTT terputus
        Coba hubungkan ke broker yang dikonfigurasi
        JIKA tersambung
            Langganan ke keempat topik perintah relay
            Atur indikator MQTT ke HIGH
        LAINNYA
            Atur indikator MQTT ke LOW
            Tunggu 5 detik
        ENDIF
    ENDWHILE
END FUNGSI

FUNGSI onMqttMessage(topic, payload)
    Ubah byte payload menjadi teks lalu cetak topik dan pesan

    Untuk relay yang sesuai dengan topik:
        JIKA payload == "1"
            Atur pin relay ke LOW (ON)
            Terbitkan status ON yang sesuai
        LAINNYA
            Atur pin relay ke HIGH (OFF)
            Terbitkan status OFF yang sesuai
        ENDIF
END FUNGSI
```

Perintah relay MQTT tidak dibatasi oleh mode otomatis/manual. Logika sensor otomatis dapat mengubah kembali status relay pompa, kabut, atau LED.

---

## 4. Pembacaan Sensor

```text
FUNGSI readDhtSensor()
    Baca kelembapan dan suhu
    JIKA salah satu pembacaan tidak valid
        Cetak error lalu kembali
    ENDIF
    Simpan kelembapan dan suhu terbaru
    Terbitkan suhu dan kelembapan

    JIKA kelembapan <= 60%
        Nyalakan relay kabut; terbitkan "Aktif"
    LAINNYA
        Matikan relay kabut; terbitkan "Mati"
    ENDIF
END FUNGSI

FUNGSI readSoilMoistureSensor()
    Baca nilai ADC dari GPIO 35
    Petakan kalibrasi udara kering 2290 ke 0%, terendam air 355 ke 100%
    Batasi hasil ke 0%-100%
    Simpan dan terbitkan persentase kelembapan tanah
END FUNGSI

FUNGSI readWaterLevelSensor()
    Baca hitungan ADC dari GPIO 34
    Simpan dan terbitkan hitungan mentah
    JIKA level air <= 1000
        Nyalakan relay solenoid; terbitkan "Open"
    LAINNYA
        Matikan relay solenoid; terbitkan "Close"
    ENDIF
END FUNGSI

FUNGSI readLightSensor()
    Baca lux dari BH1750
    JIKA lux tidak valid
        Cetak error
    LAINNYA
        Simpan dan terbitkan lux
        PANGGIL updateLedState()
    ENDIF
END FUNGSI

FUNGSI readTdsSensor()
    Tambahkan satu pembacaan ADC ke buffer melingkar 30 elemen
    Rata-ratakan buffer, ubah rata-rata ADC menjadi tegangan, lalu hitung TDS ppm
    Simpan dan terbitkan TDS
    PANGGIL updatePumpState()
END FUNGSI
```

---

## 5. Kendali Otomatis Pompa

```text
FUNGSI updatePumpState()
    JIKA level air, TDS, atau kelembapan tanah kurang dari 0
        KEMBALI
    ENDIF

    waterLevelInRange = (1000 <= level air <= 2000)
    tdsInRange = (50 <= TDS <= 300)
    soilInRange = (50 <= kelembapan tanah <= 70)
    soilOver = (kelembapan tanah >= 70)

    JIKA waterLevelInRange DAN tdsInRange
        JIKA soilInRange ATAU soilOver
            Matikan relay pompa; terbitkan "Mati"
        LAINNYA
            Nyalakan relay pompa; terbitkan "Aktif"
        ENDIF
    LAINNYA
        Matikan relay pompa; terbitkan "Mati"
    ENDIF
END FUNGSI
```

Karena `soilInRange` ATAU `soilOver` mencakup semua nilai mulai dari 50%, aturan tanah yang efektif adalah: pompa ON di bawah 50%, OFF pada atau di atas 50%. Level air dan TDS juga harus berada dalam rentang inklusifnya. Fungsi ini dipanggil setelah setiap pembaruan TDS.

---

## 6. Kendali Otomatis LED

```text
FUNGSI updateLedState()
    JIKA suhu atau lux kurang dari 0
        KEMBALI
    ENDIF

    temperatureInRange = (18 <= suhu <= 27)
    temperatureOver = (suhu >= 27)
    luxInRange = (2000 <= lux <= 5000)
    luxOver = (lux >= 5000)

    JIKA temperatureInRange DAN luxInRange
        Matikan relay LED; terbitkan "Mati"
    ELSE IF luxOver ATAU temperatureOver
        Matikan relay LED; terbitkan "Mati"
    LAINNYA
        Nyalakan relay LED; terbitkan "Aktif"
    ENDIF
END FUNGSI
```

Fungsi ini dipanggil setelah pembacaan BH1750 yang valid. Suhu diperbarui lebih awal dalam siklus sensor yang sama.

---

## 7. Loop Utama

```text
intervalPembacaanSensor = 2000 ms
lastSensorRead = 0

ULANGI TERUS
    JIKA MQTT terputus
        Hubungkan ulang ke MQTT
    ENDIF
    Proses pesan MQTT yang masuk

    JIKA waktu sekarang - lastSensorRead >= intervalPembacaanSensor
        Perbarui lastSensorRead
        Baca DHT22
        Baca kelembapan tanah
        Baca level air dan kendalikan solenoid
        Baca cahaya dan kendalikan LED
        Baca TDS dan kendalikan pompa
        Cetak baris kosong
    ENDIF
SELESAI ULANGAN
```

---

## 8. Catatan Perilaku Saat Ini

- Tidak ada setup atau pembaruan WS2812B yang aktif dalam sketch saat ini.
- Fungsi `updateSolenoidState()` terpisah telah didefinisikan, tetapi pembacaan sensor mengendalikan solenoid secara langsung; fungsi tersebut tidak dipanggil.
- Perintah relay MQTT dapat diterima dalam mode apa pun. Sketch saat ini tidak memiliki topik atau flag mode manual.
