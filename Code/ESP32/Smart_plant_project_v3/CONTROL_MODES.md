# Mode kontrol pompa (tahap 1)

Firmware sekarang menerima mode melalui MQTT. Upload ulang sketch untuk menerapkan perubahan. Ini hanya pemilihan mode, belum penerima perintah predictive pump.

## Pengujian di Node-RED

1. Buat tiga Inject node dengan payload bertipe **string**: `off`, `rule_based`, `predictive`. Jangan beri tanda kutip sebagai bagian isi payload, jangan gunakan JSON object, dan nonaktifkan inject otomatis saat startup.
2. Hubungkan ke MQTT out menggunakan broker yang sama dengan ESP32, topik `/smartplant/esp32-01/control/mode`, retain **false**, QoS 0.
3. Tambahkan MQTT in pada `/smartplant/esp32-01/status/control`, lalu JSON dan Debug.
4. Deploy. Uji `off`, `predictive`, lalu `rule_based` dalam pengawasan. Uji awal dengan pompa dilepas.

Status contoh:

```json
{"mode":"predictive","predictive_ready":false,"pump_on":false}
```

- Setelah reboot, mode selalu `off`. Pilih mode secara eksplisit.
- `off`: tidak ada pemicu pompa otomatis.
- `rule_based`: aturan lama kelembapan <=60, water level >=250, TDS <=450 dapat memicu pulse pompa pada pembacaan sensor berikutnya.
- `predictive`: aturan lama tidak memicu pompa; pompa tetap mati karena command handler predictive belum dibuat.
- Setiap permintaan mode valid menghentikan pulse aktif. Payload tidak dikenal ditolak, mode tidak berubah.
- Mist, LED, dan fan tetap memakai aturan sebelumnya; `off` hanya mengacu pada pompa.
- Firmware berlangganan ulang dan melaporkan mode saat reconnect. Mode dalam RAM tetap berlaku saat reconnect; reboot mengembalikan ke `off`.
- Hapus retained message lama pada topik mode jika pernah dibuat. PubSubClient callback ini tidak menyediakan retained flag; instruksi retain false berlaku pada pengirim, bukan jaminan penolakan pesan retained oleh firmware.
- Sebelum masuk reconnect MQTT yang masih blocking, firmware mematikan pompa. Reconnect nonblocking, cooldown, interlock sensor fresh, ID command, expiry, dan acknowledgement command tetap perlu ditambahkan saat implementasi predictive pump.
- Status control adalah snapshot saat mode berubah/reconnect. Untuk kejadian pompa ON/OFF, gunakan topik `/smartplant/esp32-01/notification/pump` yang sudah ada.

Tidak menambah library baru. Build/upload dan perilaku hardware harus diverifikasi pada board; tidak ada flashing otomatis dari perubahan ini.
