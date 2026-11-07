# Mode kontrol pompa dan predictive command

Firmware menerima mode dan predictive pump command melalui MQTT. Install **ArduinoJson 7** di Arduino IDE Library Manager, kemudian Verify dan Upload ulang sketch. Command sender tersedia di folder dashboard `forecasting/node_red_pump_command.js`.

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
- `predictive`: aturan lama tidak memicu pompa; hanya predictive command yang lolos pemeriksaan dapat memicu pompa.
- Setiap permintaan mode valid menghentikan pulse aktif. Payload tidak dikenal ditolak, mode tidak berubah.
- Mist, LED, dan fan tetap memakai aturan sebelumnya; `off` hanya mengacu pada pompa.
- Firmware berlangganan ulang dan melaporkan mode saat reconnect. Mode dalam RAM tetap berlaku saat reconnect; reboot mengembalikan ke `off`.
- Hapus retained message lama pada topik mode jika pernah dibuat. PubSubClient callback ini tidak menyediakan retained flag; instruksi retain false berlaku pada pengirim, bukan jaminan penolakan pesan retained oleh firmware.
- Firmware mematikan pompa sebelum reconnect. Percobaan reconnect dijadwalkan setiap 5 detik tanpa while/delay; satu network connect masih dapat menunggu socket timeout. Timer ESP terpisah mematikan relay saat jaringan menahan main loop.
- Status control dikirim saat mode berubah/reconnect dan setiap siklus sensor (sekitar 5 detik). Untuk kejadian pompa ON/OFF, gunakan topik `/smartplant/esp32-01/notification/pump` yang sudah ada.

## Wiring predictive sender

```
Prediction MQTT -> JSON -> Gradient Policy Preview --+
                                                    +-> Pump Command Function (2 outputs)
Status/control MQTT -> JSON -------------------------+       |1 -> MQTT out
                                                            |2 -> Debug
```

Pertahankan `msg.topic` status/control supaya Function dapat mengenali input status. MQTT out topik `/smartplant/esp32-01/control/pump`, QoS 0, retain false. Jangan tambahkan JSON node sebelum MQTT out; sender sudah menghasilkan JSON string. Pertahankan cabang penyimpanan InfluxDB yang sudah ada.

Sender default `ENABLE_COMMANDS = false`: hanya menampilkan alasan tidak mengirim. Saat firmware/status sudah diuji dan pompa dilepas untuk tes relay, ubah menjadi true dan Deploy. Deployment dapat memicu pulse pertama pada prediksi baru yang memenuhi syarat. Jangan sambungkan output 2 ke MQTT aktuator.

Subscribe `/smartplant/esp32-01/status/pump-command` -> JSON -> Debug untuk acknowledgement accepted/rejected. `request_sent` pada Debug sender bukan bukti pompa bekerja; accepted adalah bukti firmware memulai pulse, bukan bukti aliran air. Notifikasi OFF dapat terlambat jika loop sedang menunggu jaringan, meskipun timer sudah mematikan relay.

## Pemeriksaan dan batas

- Model harus gradient_boosting, horizon 10m, predicted moisture 0..60. Origin forecast tidak lebih tua 90 detik.
- ESP32 membutuhkan clock NTP yang valid (`clock_ready`), network dapat mengakses pool.ntp.org atau time.nist.gov. Ubah server ke NTP lokal bila diperlukan. Clock Node-RED dan service harus sinkron.
- Reservoir >=250 dan <4095 (4095 diperlakukan saturasi), TDS 0..450, moisture 0..<70, sensor batch tidak lebih tua 15 detik.
- TDS menunggu 30 sampel asli mengisi buffer: sekitar 2,5 menit setelah boot. `sensors_safe` tetap false sampai warmup selesai atau jika interlock gagal.
- `predictive_ready` berarti mode predictive, clock/sensor valid, cooldown selesai, pompa idle, dan timer tersedia. Nilai false selama cooldown adalah normal.
- Pulse maksimum 500ms, cooldown predictive 300000ms dihitung sejak pompa mulai. Aktivasi rule_based juga memulai penghitung, tetapi aturan lama tidak dibatasi cooldown baru agar baseline tetap dapat dibandingkan.
- Session acak diperbarui saat reconnect/perubahan mode. Perintah kedaluwarsa setelah <=15 detik; origin yang sudah diterima atau lebih tua ditolak. Mode off/rule_based menolak perintah predictive. Jangan gunakan retained commands.
- Reboot menghapus cooldown RAM dan mode kembali off. Hindari menyalakan ulang perangkat untuk melewati cooldown. Tidak ada jaminan pemulihan cooldown setelah kehilangan daya.
- Sender juga memberlakukan cooldown 5 menit setelah request, walaupun request hilang/ditolak. Ini sengaja konservatif. Firmware tetap otoritas terakhir dan tidak mempercayai cooldown sender.
- Kontrol ini memakai broker lokal yang ada, tanpa menambahkan autentikasi/ACL. Session mencegah replay lintas koneksi, bukan pengganti autentikasi broker. Jangan gunakan public broker untuk actuator commands.

## Uji sebelum pompa dipasang kembali

1. Compile/upload; pilih off dan periksa relay mati.
2. Pilih predictive. Tunggu NTP dan buffer TDS; lihat status setiap 5 detik.
3. Sender disabled: output `observation_only_commands_disabled` ketika semua kondisi siap.
4. Pompa dilepas: enable sender, tunggu forecast baru <=60. Periksa accepted, relay pulse kemudian OFF dan cooldown_remaining_ms menurun.
5. Ulang command yang sama: rejected (duplicate/cooldown). Uji expired command, mode off, reservoir rendah, atau TDS melebihi batas: tidak boleh menghasilkan pulse.
6. Uji disconnect MQTT saat pulse dengan pompa dilepas: relay harus kembali OFF oleh timer. Setelah reconnect session berubah.
7. Sambungkan pompa untuk uji penyiraman terawasi, ukur durasi aktual dan respons kelembapan sebelum menyesuaikan threshold/cooldown.

Build/upload dan perilaku hardware harus diverifikasi pada board; tidak ada flashing otomatis dari perubahan ini. API: https://arduinojson.org/v7/api/jsondocument/ dan https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Time/SimpleTime/SimpleTime.ino
