"""Offline EDA/training and live MQTT moisture forecasting. Python 3.10+."""
import argparse
import json
import logging
import math
import os
from pathlib import Path
import threading
import time

import joblib
import numpy as np
import pandas as pd
from sklearn.ensemble import HistGradientBoostingRegressor, RandomForestRegressor
from sklearn.metrics import mean_absolute_error, mean_squared_error

# Kolom sensor yang wajib tersedia; moisture menjadi variabel yang diprediksi.
SENSORS = ['moisture', 'temperature', 'humidity', 'light']
# Jarak prediksi ke depan dalam menit; dibuat satu model per horizon.
HORIZONS = [5, 10, 15]
# Jumlah menit berurutan yang dibutuhkan untuk membentuk fitur.
HISTORY = 30
LOG = logging.getLogger(__name__)


def clean(raw, device):
    """Memilih data satu perangkat dan membersihkan waktu serta nilai sensor.

    Nilai tidak valid menjadi NaN (data hilang), bukan diganti dengan nol.
    Hasilnya memakai waktu UTC sebagai indeks agar bisa dikelompokkan per menit."""
    # Validasi struktur CSV sebelum memilih perangkat yang diminta.
    required = ['time', 'device', *SENSORS]
    missing = set(required) - set(raw.columns)
    if missing:
        raise ValueError(f'Missing columns: {sorted(missing)}')
    frame = raw.loc[raw.device == device, required].copy()
    frame['time'] = pd.to_datetime(frame.time, utc=True, errors='coerce')
    frame = frame.dropna(subset=['time']).sort_values('time')
    for col in SENSORS:
        frame[col] = pd.to_numeric(frame[col], errors='coerce')
    frame[SENSORS] = frame[SENSORS].replace([np.inf, -np.inf], np.nan)
    for col in ['moisture', 'humidity']:
        frame.loc[~frame[col].between(0, 100), col] = np.nan
    frame.loc[frame.light < 0, 'light'] = np.nan
    frame.loc[~frame.temperature.between(-40, 80), 'temperature'] = np.nan
    # Jika ada waktu duplikat, pertahankan baris terakhir pada urutan yang ada.
    return frame.drop_duplicates('time', keep='last').set_index('time')[SENSORS]


def minute_data(frame):
    """Menghitung rata-rata setiap sensor dalam interval satu menit.

    Label T mewakili interval [T-1 menit, T), sehingga data tepat pada T
    masuk interval berikutnya. Setiap sensor perlu minimal tiga nilai valid."""
    """Right-labelled completed bins: timestamp T summarizes [T-1min, T)."""
    means = frame.resample('1min', closed='left', label='right').mean()
    counts = frame.resample('1min', closed='left', label='right').count()
    # Tidak ada interpolasi: menit kosong atau terlalu sedikit sampel tetap NaN.
    # Kekosongan ini membuat jendela riwayat/target terkait tidak memenuhi syarat.
    return means.where(counts >= 3)


def features(minutes):
    """Mengubah riwayat sensor menjadi fitur masukan model.

    Lag adalah nilai beberapa menit sebelumnya; lag 0 adalah menit terkini
    yang sudah selesai. Mean, std, dan change merangkum level, variasi,
    serta perubahan sensor. Hanya riwayat 30 menit lengkap yang diterima."""
    columns = {}
    for sensor in SENSORS:
        for lag in [0, 1, 2, 5, 10, 15, 20, 29]:
            columns[f'{sensor}_lag_{lag}'] = minutes[sensor].shift(lag)
        for window in [5, 15, 30]:
            columns[f'{sensor}_mean_{window}'] = minutes[sensor].rolling(window).mean()
            columns[f'{sensor}_std_{window}'] = minutes[sensor].rolling(window).std()
        columns[f'{sensor}_change_5'] = minutes[sensor] - minutes[sensor].shift(5)
    x = pd.DataFrame(columns, index=minutes.index)
    # Seluruh sensor harus valid pada setiap menit dalam 30 menit terakhir.
    complete = minutes.notna().all(axis=1).rolling(HISTORY).sum().eq(HISTORY)
    return x.loc[complete].dropna()


def supervised(minutes):
    """Memasangkan fitur saat ini (X) dengan kelembapan masa depan (y).

    Target untuk setiap horizon adalah rata-rata moisture pada menit tersebut,
    bukan rata-rata seluruh periode menuju horizon. Semua sensor harus lengkap
    dari waktu asal sampai 15 menit ke depan agar pasangan data diterima."""
    x = features(minutes)
    # shift(-h) menempatkan nilai h menit mendatang pada baris waktu asal.
    y = pd.DataFrame({h: minutes.moisture.shift(-h) for h in HORIZONS})
    # Pemeriksaan dari arah belakang memastikan seluruh menit menuju target ada.
    valid = minutes.notna().all(axis=1).iloc[::-1].rolling(max(HORIZONS) + 1).sum().iloc[::-1]
    keep = x.index.intersection(y.index[valid.eq(max(HORIZONS) + 1)])
    return x.loc[keep], y.loc[keep]


def candidates():
    """Membuat model baru untuk setiap proses pelatihan.

    Random Forest menggabungkan prediksi banyak pohon; gradient boosting
    membangun pohon bertahap untuk memperbaiki kesalahan sebelumnya.
    random_state membuat pengacakan dapat diulang dengan konfigurasi sama."""
    return {
        'random_forest': RandomForestRegressor(n_estimators=150, max_depth=12,
                                               min_samples_leaf=5, random_state=42, n_jobs=-1),
        'gradient_boosting': HistGradientBoostingRegressor(max_iter=150, max_leaf_nodes=15,
                                                          l2_regularization=1, early_stopping=False,
                                                          random_state=42),
    }


def scores(actual, predicted):
    """Menghitung kesalahan prediksi: MAE dan RMSE; semakin kecil semakin baik.

    MAE adalah rata-rata selisih absolut, sedangkan RMSE memberi penalti
    lebih besar pada kesalahan besar. Satuannya mengikuti moisture
    (poin persentase jika moisture dinyatakan dalam persen)."""
    return {'mae': float(mean_absolute_error(actual, predicted)),
            'rmse': float(np.sqrt(mean_squared_error(actual, predicted)))}


def train(args):
    """Menjalankan EDA, pemilihan model, evaluasi, dan penyimpanan model.

    Urutan waktu dipertahankan: sekitar 70% awal untuk train, 15% berikutnya
    untuk validasi, dan 15% akhir untuk test, sebelum pembuangan batas label.
    Model dipilih terpisah untuk horizon 5, 10, dan 15 menit."""
    import matplotlib
    # Backend noninteraktif: grafik disimpan ke berkas tanpa membuka jendela.
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    raw = pd.read_csv(args.csv)
    frame = clean(raw, args.device)
    if frame.empty:
        raise ValueError('No usable rows for requested device')
    minutes = minute_data(frame)
    # Simpan hasil eksplorasi data: agregasi, statistik, jeda, dan grafik sensor.
    minutes.to_csv(out / 'minute_data.csv')
    frame.describe().to_csv(out / 'sensor_summary.csv')
    gaps = frame.index.to_series().diff().dt.total_seconds()
    gaps[gaps > 60].to_csv(out / 'recording_gaps_seconds.csv')
    axes = minutes.plot(subplots=True, figsize=(14, 10), title='One-minute sensor means (UTC); gaps preserved')
    axes[0].figure.tight_layout()
    axes[0].figure.savefig(out / 'eda.png', dpi=140)
    plt.close('all')

    x, y = supervised(minutes)
    if len(x) < 300:
        raise ValueError(f'Only {len(x)} eligible windows; collect more continuous data (need >=300).')
    val_start = x.index[int(len(x) * .70)]
    test_start = x.index[int(len(x) * .85)]
    # Buang sampel yang target 15 menitnya mencapai periode berikutnya.
    # Ini mencegah label train/validasi mengambil data dari periode sesudahnya.
    train_mask = x.index + pd.Timedelta(minutes=max(HORIZONS)) < val_start
    val_mask = (x.index >= val_start) & (x.index + pd.Timedelta(minutes=max(HORIZONS)) < test_start)
    test_mask = x.index >= test_start
    # Data pengembangan menggabungkan train dan validasi sebelum periode test.
    dev_mask = x.index + pd.Timedelta(minutes=max(HORIZONS)) < test_start
    if min(train_mask.sum(), val_mask.sum(), test_mask.sum()) < 30:
        raise ValueError('Insufficient chronological split after label purging.')
    report = {'device': args.device, 'raw_rows': len(raw), 'clean_rows': len(frame),
              'eligible_windows': len(x), 'validation_start_utc': str(val_start),
              'test_start_utc': str(test_start), 'split_counts': {
                  'train': int(train_mask.sum()), 'validation': int(val_mask.sum()),
                  'test': int(test_mask.sum())}, 'horizons': {}}
    artifact = {'device': args.device, 'sensors': SENSORS, 'feature_names': list(x.columns),
                'trained_through_utc': str(frame.index.max()), 'models': {},
                'ml_candidates': {}, 'selected': {}, 'version': 1}
    predictions = pd.DataFrame(index=x.index[test_mask])
    # Pemilihan dan evaluasi dilakukan secara terpisah untuk setiap horizon.
    for horizon in HORIZONS:
        target = y[horizon]
        # Persistence menganggap moisture masa depan sama dengan nilai saat ini.
        # Baseline ini ikut dipilih jika MAE validasinya paling kecil.
        validation = {'persistence': scores(target[val_mask], x.loc[val_mask, 'moisture_lag_0'])}
        for name, model in candidates().items():
            model.fit(x[train_mask], target[train_mask])
            validation[name] = scores(target[val_mask], model.predict(x[val_mask]))
        # Pemilihan hanya memakai validasi; skor test tidak menentukan pemenang.
        winner = min(validation, key=lambda name: validation[name]['mae'])
        baseline = x.loc[test_mask, 'moisture_lag_0'].to_numpy()
        test_scores = {'persistence': scores(target[test_mask], baseline)}
        deployment_candidates = {}
        predicted = baseline
        for name, model in candidates().items():
            # Latih ulang dengan data pengembangan, lalu nilai pada test terpisah.
            model.fit(x[dev_mask], target[dev_mask])
            candidate_prediction = model.predict(x[test_mask])
            test_scores[name] = scores(target[test_mask], candidate_prediction)
            if name == winner:
                predicted = candidate_prediction
            # Setelah prediksi test kandidat dihitung, latih model deployment
            # dengan seluruh data yang memenuhi syarat, termasuk periode test.
            # Skor test di atas menilai model sebelum pelatihan ulang ini.
            model.fit(x, target)
            deployment_candidates[name] = model
        report['horizons'][horizon] = {'selected': winner, 'validation': validation,
                                     'test': scores(target[test_mask], predicted),
                                     'test_persistence': scores(target[test_mask], baseline),
                                     'test_all_candidates': test_scores}
        predictions[f'actual_{horizon}m'] = target[test_mask]
        predictions[f'predicted_{horizon}m'] = predicted
        predictions[f'persistence_{horizon}m'] = baseline
        # None menandakan bahwa persistence terpilih, sehingga tidak perlu model ML.
        artifact['models'][horizon] = deployment_candidates.get(winner)
        artifact['ml_candidates'][horizon] = deployment_candidates
        artifact['selected'][horizon] = winner
    predictions.to_csv(out / 'test_predictions.csv')
    fig, axes = plt.subplots(3, 1, figsize=(14, 10))
    for ax, h in zip(axes, HORIZONS):
        plot = predictions[[f'actual_{h}m', f'predicted_{h}m']].copy()
        plot.index = plot.index + pd.Timedelta(minutes=h)
        plot.plot(ax=ax, title=f'{h}-minute forecast; target time UTC')
    fig.tight_layout()
    fig.savefig(out / 'forecast_evaluation.png', dpi=140)
    plt.close(fig)
    (out / 'metrics.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    # Artefak menyimpan model, urutan fitur, identitas perangkat, dan metadata.
    joblib.dump(artifact, out / 'model.joblib')
    print(json.dumps(report, indent=2))


def load_model(path):
    """Membaca model beserta metadata dan memeriksa kompatibilitasnya.

    Gunakan berkas joblib dari sumber tepercaya karena proses pemuatan
    dapat menjalankan kode yang tersimpan di dalam berkas."""
    # Deserialisasi joblib/pickle dapat menjalankan kode; sumber harus tepercaya.
    artifact = joblib.load(path)
    if artifact.get('version') != 1 or artifact['sensors'] != SENSORS:
        raise ValueError('Incompatible model artifact')
    return artifact


def predict(artifact, minutes, origin):
    """Memprediksi moisture dari satu waktu asal menggunakan model tersimpan.

    origin adalah waktu akhir menit terakhir yang sudah selesai. Keluaran
    memuat prediksi model terpilih serta semua kandidat yang tersedia.
    Fungsi ini menghasilkan data prediksi, tanpa mengirim perintah pompa."""
    x = features(minutes)
    if origin not in x.index:
        raise ValueError('Need 30 consecutive complete minutes, >=3 observations per sensor per minute.')
    # Pertahankan format tabel satu baris dan urutan fitur seperti saat pelatihan.
    x = x.loc[[origin], artifact['feature_names']]
    forecasts = {}
    model_forecasts = []
    for h, model in artifact['models'].items():
        value = float(x['moisture_lag_0'].iloc[0] if model is None else model.predict(x)[0])
        forecasts[f'{h}m'] = {'moisture': value,
                             'target_time_utc': (origin + pd.Timedelta(minutes=h)).isoformat()}
        values = {'persistence': float(x['moisture_lag_0'].iloc[0])}
        for name, candidate in artifact.get('ml_candidates', {}).get(h, {}).items():
            values[name] = float(candidate.predict(x)[0])
        for name, candidate_value in values.items():
            if not math.isfinite(candidate_value):
                raise ValueError(f'Non-finite prediction from {name} at {h} minutes')
            model_forecasts.append({'model': name, 'horizon': f'{h}m',
                                    'moisture': candidate_value,
                                    'target_time_utc': forecasts[f'{h}m']['target_time_utc']})
    return {'device': artifact['device'], 'origin_utc': origin.isoformat(),
            'selected_models': artifact.get('selected', {}),
            'generated_at_utc': pd.Timestamp.now(tz='UTC').isoformat(),
            'target_definition': 'mean moisture during minute ending at target_time_utc',
            'current_minute_moisture': float(x['moisture_lag_0'].iloc[0]),
            'forecasts': forecasts, 'model_forecasts': model_forecasts}


def forecast_csv(args):
    """Menghasilkan prediksi offline dari CSV menggunakan model yang sudah dilatih.

    --at dapat menentukan waktu asal historis. Tanpa --at, gunakan batas
    menit terakhir pada CSV agar menit mentah yang masih parsial tidak dipakai."""
    artifact = load_model(args.model)
    raw = clean(pd.read_csv(args.csv), artifact['device'])
    if raw.empty:
        raise ValueError('No usable rows for model device')
    minutes = minute_data(raw)
    # Menit terakhir CSV mungkin belum selesai; ambil batas menit sebelumnya.
    origin = pd.Timestamp(args.at) if args.at else raw.index.max().floor('min')
    if origin.tzinfo is None:
        raise ValueError('--at must include a timezone, for example 2026-11-06T19:40:00Z')
    origin = origin.tz_convert('UTC')
    if origin != origin.floor('min') or origin > raw.index.max().floor('min'):
        raise ValueError('Forecast origin must be a completed minute within the CSV range')
    print(json.dumps(predict(artifact, minutes.loc[:origin], origin), indent=2))


def serve(args):
    """Mengumpulkan sensor melalui MQTT dan menerbitkan prediksi setiap menit.

    Waktu penerimaan pesan di komputer dipakai sebagai waktu sampel.
    Riwayat dikosongkan setelah tersambung kembali, sehingga layanan perlu
    mengumpulkan ulang 30 menit lengkap sebelum dapat memprediksi."""
    import paho.mqtt.client as mqtt

    artifact = load_model(args.model)
    root = f"/smartplant/{artifact['device']}"
    if not all(set(artifact.get('ml_candidates', {}).get(h, {})) >=
               {'random_forest', 'gradient_boosting'} for h in HORIZONS):
        LOG.warning('Artifact lacks some ML candidates; retrain to publish all nine forecasts')
    topic_fields = {f'{root}/sensor/{s}': s for s in SENSORS}
    samples = {s: [] for s in SENSORS}
    # Callback MQTT dan loop prediksi berjalan di thread berbeda.
    # Lock melindungi buffer agar tidak dibaca saat sedang diubah.
    lock = threading.Lock()
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                         client_id=f"moisture-forecast-{artifact['device']}")
    if args.username:
        client.username_pw_set(args.username, os.environ.get('MQTT_PASSWORD'))
    if args.tls:
        client.tls_set()

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            LOG.error('MQTT connection rejected: %s', reason_code)
            return
        with lock:
            for values in samples.values():
                values.clear()
        client.subscribe([(topic, 0) for topic in topic_fields])
        LOG.info('Connected; collecting 30 complete minutes of fresh sensor history')

    def on_message(client, userdata, message):
        # Abaikan retained message karena bisa berisi nilai lama dari broker.
        if message.retain or message.topic not in topic_fields:
            return
        try:
            value = float(message.payload.decode('utf-8'))
            if not math.isfinite(value):
                return
        except (ValueError, UnicodeError):
            return
        now = pd.Timestamp.now(tz='UTC')
        with lock:
            samples[topic_fields[message.topic]].append((now, value))

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.host, args.port, 60)
    client.loop_start()
    last_origin = None
    try:
        while True:
            origin = pd.Timestamp.now(tz='UTC').floor('min')
            if origin != last_origin:
                last_origin = origin
                # Salin buffer di dalam lock; perhitungan berikutnya memakai salinan.
                with lock:
                    cutoff = origin - pd.Timedelta(minutes=HISTORY + 2)
                    for sensor in SENSORS:
                        samples[sensor] = [(t, v) for t, v in samples[sensor] if t >= cutoff]
                    snapshot = {s: list(values) for s, values in samples.items()}
                try:
                    if not all(snapshot.values()):
                        raise ValueError('Waiting for sensor messages')
                    # Topik sensor datang terpisah; agregasikan tiap sensor dengan
                    # aturan interval, batas validitas, dan minimum sampel yang sama.
                    columns = {}
                    for sensor, values in snapshot.items():
                        series = pd.Series([v for _, v in values],
                                           index=pd.DatetimeIndex([t for t, _ in values]))
                        if sensor in ['moisture', 'humidity']:
                            series = series.where(series.between(0, 100))
                        elif sensor == 'temperature':
                            series = series.where(series.between(-40, 80))
                        else:
                            series = series.where(series >= 0)
                        resampler = series.resample('1min', closed='left', label='right')
                        columns[sensor] = resampler.mean().where(resampler.count() >= 3)
                    # Bentuk tepat 30 menit; reindex menampakkan menit hilang sebagai NaN.
                    minutes = pd.DataFrame(columns).reindex(
                        pd.date_range(origin - pd.Timedelta(minutes=HISTORY - 1), origin, freq='1min'))
                    payload = predict(artifact, minutes, origin)
                    # Terbitkan JSON hasil prediksi ke topik khusus prediksi moisture.
                    # QoS 1 meminta konfirmasi broker; retain=False tidak menyimpan pesan
                    # sebagai retained message. Ini bukan konfirmasi penerimaan konsumen.
                    info = client.publish(f'{root}/prediction/moisture', json.dumps(payload), qos=1, retain=False)
                    info.wait_for_publish(timeout=10)
                    if not info.is_published():
                        raise ValueError('MQTT publish timed out')
                    LOG.info('Published %s', json.dumps(payload))
                except ValueError as exc:
                    LOG.info('No forecast: %s', exc)
            time.sleep(1)
    except KeyboardInterrupt:
        LOG.info('Stopping')
    finally:
        # Tutup koneksi dan thread jaringan juga ketika layanan dihentikan.
        client.disconnect()
        client.loop_stop()


def main():
    """Membaca pilihan terminal dan menjalankan mode train, predict, atau serve.

    set_defaults menyimpan fungsi tujuan pada args.func; pemanggilan
    args.func(args) kemudian menjalankan fungsi sesuai subperintah."""
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    training = sub.add_parser('train')
    training.add_argument('--csv', required=True)
    training.add_argument('--device', default='esp32-01')
    training.add_argument('--output', default='outputs')
    training.set_defaults(func=train)
    offline = sub.add_parser('predict')
    offline.add_argument('--csv', required=True)
    offline.add_argument('--model', default='outputs/model.joblib')
    offline.add_argument('--at', help='Optional historical origin with timezone, aligned to a minute')
    offline.set_defaults(func=forecast_csv)
    service = sub.add_parser('serve')
    service.add_argument('--model', default='outputs/model.joblib')
    service.add_argument('--host', required=True)
    service.add_argument('--port', type=int, default=1883)
    service.add_argument('--username', default=os.environ.get('MQTT_USERNAME'))
    service.add_argument('--tls', action='store_true')
    service.set_defaults(func=serve)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(levelname)s %(message)s')
    args.func(args)


# Jalankan CLI hanya saat berkas dieksekusi langsung, bukan saat diimpor.
if __name__ == '__main__':
    main()
