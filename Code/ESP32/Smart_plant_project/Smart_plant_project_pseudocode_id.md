# Pseudocode Smart Plant Project

## Gambaran Umum

ESP32 membaca sensor, menerbitkan hasil pembacaan melalui MQTT, dan menerima perintah MQTT untuk tiga relay aktif-low: pompa, generator kabut, dan katup solenoid. Rutinitas sensor mengendalikan relay tersebut secara otomatis.

---

## 1. Perangkat Keras dan Ambang Batas

```text
Indikator status:
    WiFi: GPIO 0
    MQTT: GPIO 2

Keluaran relay aktif-low:
    Pompa: GPIO 12
    Generator kabut: GPIO 14
    Katup solenoid: GPIO 26

DHT22: GPIO 4
    Kabut ON saat kelembapan <= 60%
    Kode juga memeriksa kelembapan >= 80% untuk mematikan kabut
    Konstanta suhu 18 C dan 27 C tidak digunakan oleh kendali aktif

Sensor kelembapan tanah kapasitif: ADC GPIO 35
    Pembacaan udara kering: 2290
    Pembacaan terendam penuh: 355

Sensor level air: ADC GPIO 34
    Rentang level air yang mengizinkan pompa: 1000-2000 hitungan ADC, inklusif
    Ambang solenoid ON: <= 1000
    Ambang solenoid OFF: >= 1500

Sensor cahaya BH1750: SDA GPIO 32, SCL GPIO 33
Sensor TDS: ADC GPIO 39
    Referensi tegangan: 3.3 V
    Skala ADC: 4095
    Buffer rata-rata: 30 pembacaan
```

Topik MQTT memakai prefiks `/smartplant/esp32-01/`. Topik perintah relay yang dilanggan adalah `/command/relay/1/1` (pompa), `/command/relay/1/2` (kabut), dan `/command/relay/2/2` (solenoid). Pembacaan sensor diterbitkan ke `/sensor/temperature`, `/sensor/humidity`, `/sensor/moisture`, `/sensor/water-level`, `/sensor/light`, dan `/sensor/ppm`. Status relay diterbitkan ke `/notification/pump`, `/notification/mist`, dan `/notification/solenoid`.

---

## 2. Startup dan Koneksi MQTT

```text
Inisialisasi komunikasi serial
Atur indikator WiFi/MQTT dan tiga pin relay sebagai OUTPUT
Atur semua relay aktif-low ke HIGH (OFF)
Hubungkan ke WiFi dan tunggu sampai tersambung
Pilih broker MQTT cloud atau lokal sesuai konfigurasi
Atur server dan callback MQTT
Inisialisasi DHT22
Atur pin sensor level air sebagai INPUT
Inisialisasi I2C dan BH1750
    Jika inisialisasi BH1750 gagal, cetak error dan tunggu tanpa batas

FUNGSI reconnect()
    SELAMA MQTT terputus
        Coba hubungkan ke broker
        JIKA koneksi berhasil
            Langganan ke tiga topik perintah relay
            Atur indikator MQTT ke HIGH
        LAINNYA
            Atur indikator MQTT ke LOW
            Cetak status koneksi dan tunggu 5 detik
        ENDIF
    ENDWHILE
END FUNGSI
```

Setup WiFi menunggu tanpa batas sampai WiFi tersambung. Koneksi ulang MQTT memblokir program dalam pengulangan sampai koneksi berhasil.

---

## 3. Handler Perintah Relay MQTT

```text
FUNGSI mqtt_callback(topic, payload)
    Ubah byte payload menjadi teks
    Cetak topik dan payload yang diterima
    Cocokkan topik dengan relay pompa, kabut, atau solenoid

    JIKA payload == "1"
        Atur GPIO relay terkait ke LOW (ON)
        Terbitkan status ON yang sesuai
    LAINNYA
        Atur GPIO relay terkait ke HIGH (OFF)
        Terbitkan status OFF yang sesuai
    ENDIF
END FUNGSI
```

Payload selain tepat `"1"` akan mematikan relay yang dipilih. Perintah tidak memeriksa mode manual/otomatis. Logika sensor dapat mengubah status relay setelahnya.

---

## 4. Rutinitas Pembacaan Sensor

```text
FUNGSI dht22Sensor()
    Baca kelembapan dan suhu dari DHT22
    JIKA salah satu pembacaan tidak valid
        Cetak error lalu kembali
    ENDIF
    Simpan kelembapan dan suhu terbaru
    Terbitkan suhu dan kelembapan
    Cetak kedua nilai

    JIKA kelembapan <= 60%
        Nyalakan relay kabut; terbitkan "Aktif"
    ELSE IF kelembapan >= 80%
        Matikan relay kabut; terbitkan "Mati"
    LAINNYA
        Matikan relay kabut; terbitkan "Mati"
    ENDIF
END FUNGSI
```

Perilaku efektif kabut: ON pada kelembapan <= 60%, OFF di atas 60%. Kondisi 80% tidak membentuk histeresis karena cabang terakhir juga mematikan kabut.

```text
FUNGSI capacitiveSoilSensor()
    Baca nilai ADC GPIO 35
    Petakan kalibrasi udara kering 2290 ke 0% dan terendam 355 ke 100%
    Batasi persentase kelembapan ke 0%-100%
    Simpan dan terbitkan persentase
    Cetak pembacaan mentah dan persentase hasil konversi
END FUNGSI

FUNGSI waterLevelSensor()
    Baca hitungan ADC GPIO 34
    Simpan dan terbitkan hitungan mentah
    Cetak nilai level air

    JIKA level air <= 1000
        Nyalakan relay solenoid; terbitkan "Open"
    ELSE IF level air >= 1500
        Matikan relay solenoid; terbitkan "Close"
    LAINNYA
        Matikan relay solenoid; terbitkan "Close"
    ENDIF
END FUNGSI

FUNGSI lightLevelSensor()
    Baca lux dari BH1750
    JIKA lux < 0
        Cetak error
    LAINNYA
        Terbitkan dan cetak lux
    ENDIF
END FUNGSI

FUNGSI tdsMeterSensor()
    Baca ADC GPIO 39 ke slot berikutnya pada buffer melingkar 30 entri
    Rata-ratakan semua entri buffer
    tegangan = rata-rata * (3.3 / 4095)
    tds = (133.42 * tegangan^3 - 255.86 * tegangan^2 + 857.39 * tegangan) * 0.5
    Simpan dan terbitkan TDS
    Cetak tegangan dan TDS
    PANGGIL updatePumpState()
END FUNGSI
```

---

## 5. Kendali Otomatis Pompa

```text
FUNGSI updatePumpState()
    JIKA level air < 0 ATAU TDS < 0 ATAU kelembapan tanah < 0
        Kembali tanpa mengubah status pompa
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

Gabungan kondisi tanah berarti pompa ON di bawah kelembapan 50%, dan OFF pada atau di atas 50%, jika level air dan TDS juga berada dalam rentang inklusif. Fungsi dijalankan setelah setiap pembacaan TDS.

---

## 6. Loop Utama

```text
intervalSensor = 2000 ms
lastSensorRead = 0

ULANGI TERUS
    JIKA MQTT terputus
        Panggil reconnect()
    ENDIF
    Proses pesan MQTT yang masuk

    JIKA waktu sekarang - lastSensorRead >= intervalSensor
        Atur lastSensorRead = waktu sekarang
        Baca DHT22 dan kendalikan relay kabut
        Baca kelembapan tanah
        Baca level air dan kendalikan relay solenoid
        Baca cahaya dan terbitkan lux
        Baca TDS dan perbarui status pompa
        Cetak baris kosong
    ENDIF
SELESAI ULANGAN
```

---
