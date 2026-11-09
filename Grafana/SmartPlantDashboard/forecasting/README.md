# Smart Plant moisture forecasting

Python 3.10+ CLI for EDA, training, offline prediction, and live MQTT forecasts at **5, 10, and 15 minutes**. Run commands from this directory in PowerShell.

## Setup and training

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe forecast.py train --csv "H:\File\MyGithub\MyNote\Master Of Computer Science\Inteligent Internet of Things (COMP8078041)\Final Project (AOL)\AOL_Dataset.csv"
.\.venv\Scripts\python.exe -m unittest -v
```

Training creates `outputs/` with sensor summary, gap list, one-minute dataset, EDA plot, chronological validation/test metrics, held-out prediction CSV/plot, and `model.joblib`. It does not alter the source CSV.

Inputs are `time`, `device`, `moisture`, `temperature`, `humidity`, and `light`. Device defaults to `esp32-01`. Moisture is assumed to be a calibrated percentage. Sensor readings are aggregated into one-minute means, requiring at least three valid observations per sensor per minute. No missing-minute interpolation is performed. Invalid humidity/moisture outside 0–100, negative light, and temperatures outside -40–80 are treated as missing. These are basic validity checks, not proof of calibration.

The previous 30 complete minutes supply lags, rolling means/deviations, and changes. Independent models predict the minute-average moisture at each horizon. A timestamp T means the average over [T-1 minute, T); forecasting at T+5 predicts that future minute's average, not an instantaneous reading.

The first 70% of eligible origins train candidates; the next 15% select between persistence, Random Forest, and gradient boosting using MAE. The final 15% are held out for testing. Labels extending into the following period are purged. Selected models are refit on development data for testing, then on all eligible data for deployment. Test results therefore describe the evaluation fit, not a new independent evaluation of the deployment refit. No random temporal split or random early-stopping validation is used. MAE/RMSE units are moisture percentage points. Persistence can win; that indicates the ML model has not demonstrated a useful improvement.

Actuator fields are deliberately excluded because sampled notifications may not represent pulse duration. Predictions reflect the historical controller's behavior, not a counterfactual forecast with the pump disabled. Record additional realistic drying/watering cycles before relying on predictive control. This implementation does not estimate confidence intervals.

## Offline forecast

```powershell
.\.venv\Scripts\python.exe forecast.py predict --csv "H:\File\MyGithub\MyNote\Master Of Computer Science\Inteligent Internet of Things (COMP8078041)\Final Project (AOL)\AOL_Dataset.csv" --model outputs/model.joblib
```

Uses the latest completed clock minute in the CSV; fails clearly if there is insufficient recent history. It does not silently forecast from an older valid session. This is a forecast from the CSV's time, not necessarily from the current wall clock.

Your current CSV ends with a recording session shorter than 30 minutes. For a demonstration using its earlier complete session, append `--at "2026-11-06T19:40:00Z"` to the prediction command. A historical prediction using the deployment model is a functionality demonstration, not a held-out evaluation; use `test_predictions.csv` for evaluation.

## Live MQTT service

```powershell
.\.venv\Scripts\python.exe forecast.py serve --host 192.168.1.29 --model outputs/model.joblib
```

Subscribes to existing topics:

```text
/smartplant/esp32-01/sensor/moisture
/smartplant/esp32-01/sensor/temperature
/smartplant/esp32-01/sensor/humidity
/smartplant/esp32-01/sensor/light
```

Each input payload must be a plain numeric value. Live timestamps use service receipt time because these ESP32 payloads have no measurement timestamp. Keep the service computer's clock synchronized and use a low-latency connection. Retained messages are ignored. Reconnection resets history to avoid treating stale data as fresh. Startup/reconnection needs roughly 30–31 minutes to accumulate complete history. Missing/sparse minutes suppress forecasts until history is complete again.

Publishes once per minute to `/smartplant/esp32-01/prediction/moisture` with QoS 1 and retain disabled:

```json
{
  "device": "esp32-01",
  "origin_utc": "2026-11-07T03:00:00+00:00",
  "generated_at_utc": "2026-11-07T03:00:01+00:00",
  "target_definition": "mean moisture during minute ending at target_time_utc",
  "current_minute_moisture": 62.0,
  "forecasts": {
    "5m": {"moisture": 61.8, "target_time_utc": "2026-11-07T03:05:00+00:00"},
    "10m": {"moisture": 61.5, "target_time_utc": "2026-11-07T03:10:00+00:00"},
    "15m": {"moisture": 61.1, "target_time_utc": "2026-11-07T03:15:00+00:00"}
  }
}
```

Numbers above are illustrative. In Node-RED, add MQTT-in for the output topic and a JSON node, then read `msg.payload.forecasts["5m"].moisture` (and `10m`/`15m`). Persist origin time, target time, horizon, prediction, and later actual values to InfluxDB for Grafana. Deduplicate QoS 1 deliveries by device + origin + horizon. Reject stale origins in consumers.

Actual payloads also include `selected_models` so consumers can identify persistence versus ML. All fitted ML candidates are saved under `ml_candidates` in the model artifact, even if persistence wins. `metrics.json` includes held-out scores for every candidate; candidate selection still uses validation data only.

### Compare all three models live

The service now adds `model_forecasts`, an array with nine records: persistence, random_forest, and gradient_boosting at each of 5m, 10m, and 15m. Each record contains `model`, `horizon`, `moisture`, and `target_time_utc`. The original `forecasts` and `selected_models` remain unchanged and still describe validation-selected forecasts. ML comparison output does not change model selection or actuator behavior.

Restart the service to use this update. The existing artifact includes trained candidates; no retraining is needed unless the service warns that candidates are missing. Restarting requires a fresh 30–31-minute history.

Replace the prediction-formatting Function node with the contents of `node_red_forecasts.js`. Connect its output to `influxdb batch` with the existing bucket/organization and `ms` precision. Do not leave the old formatter writing the same records in parallel. This function stores all nine points with their actual model tags. Deduplicate deliveries by device + origin + horizon + model.

The forecast Flux query already groups by horizon and model. In its Grafana display-name override, use `Forecast ${__field.labels.horizon} — ${__field.labels.model}`. Initially filter the prediction query to `r.horizon == "10m"` to compare three model lines against actual moisture without nine overlapping lines. Keep the full nine-series view available for other horizons. The error-table query also retains the model label, so it can compare each candidate separately.

Offline predictions from the deployment artifact on its training period are smoke tests only, not new evidence of predictive performance. Use the held-out report or newly collected live outcomes for evaluation.

### Gradient boosting policy preview

Copy `node_red_gradient_policy.js` into a new one-output Function node called `Gradient Boosting Policy Preview`. Branch from the parsed MQTT payload BEFORE database formatting: MQTT/JSON -> policy Function -> Debug. Keep the existing database branch. Do not connect this preview output to a pump MQTT node or InfluxDB batch; its schema is a decision object.

This explicitly uses `model_forecasts` with `model=gradient_boosting` and `horizon=10m`, independently of `selected_models`. No retraining or Python changes are needed. The 10-minute horizon is an experimental starting choice. Gradient boosting has not been established as better than persistence by the current results.

Preview: predict <=60 and current <70 produces `would_water`; otherwise `hold`. Invalid/missing candidates, origins older than 90 seconds or in the future, invalid target times, and invalid moisture produce `reject`. The constants are provisional; 60 matches existing firmware while 70 is a proposed upper guard. Verify calibration and crop-specific limits. Clock synchronization is needed for age checks.

The node only describes whether moisture conditions qualify. It does NOT check reservoir/TDS, actuator state, cooldown, duplicates, or command expiry, and never sends commands. Repeated `would_water` messages are expected while the condition persists; they are not watering events. The command path is now provided separately in `node_red_pump_command.js` and updated ESP32 firmware; see the firmware folder's `CONTROL_MODES.md` for wiring, ArduinoJson 7 installation, limits, and hardware tests.

The command Function needs TWO outputs: output 1 -> MQTT out (control/pump, QoS 0, retain false); output 2 -> Debug. Feed parsed status/control MQTT messages and policy-preview decisions into it. The sender defaults to `ENABLE_COMMANDS = false`, allowing observation before physical tests. Do not enable until the updated firmware has been compiled/uploaded and the documented checks are understood. The current sketch remains off after reboot until a mode is explicitly chosen. Tests: `node test_pump_command.js`.

Status reception now accepts objects or JSON text on the exact status/control topic, and reports `status_cached` on output 2 whenever a fresh status is cached. Invalid JSON/session and unexpected topics are reported explicitly. Waiting diagnostics include cache_present and status_age_ms: false means no status was cached in this Function's context; age >15000 means updates stopped. Connect status and policy to the SAME Function instance. Context is per node, not shared by similarly named Function nodes. Replacing/redeploying the Function may require waiting for its next status message. Keep the existing live ENABLE_COMMANDS preference when copying an update; the file's default remains false.

Run `node test_gradient_policy.js` for policy regression checks.

### Store policy decisions and explanations in Grafana

Copy `node_red_decision_log.js` into a NEW one-output Function named `Decision to InfluxDB`. Branch from `Gradient Boosting Policy Preview` (not the raw forecast and not the Pump Command debug output) into this Function -> NEW `influxdb batch`. Configure the existing connection/org/bucket SmartPlantIoT and ms precision. Retain the existing branch from policy to Pump Command unchanged. The logger creates a new message, preserving the original policy message for the command branch.

Measurement: `moisture_policy_decision`; tags: device/model/horizon/stage. Fields include decision, reason_code, a human-readable explanation, exact numeric moisture/threshold inputs when available, origin/target epoch times, and decision_code (would_water=1, hold=0, reject=-1). Invalid/non-finite optional inputs are omitted, not replaced with fake zeroes. Timestamps are policy evaluation times. Same timestamp+tags overwrites the same point, making exact-message retries idempotent; independently reevaluated events may produce separate records.

These explanations describe deterministic CONTROL POLICY checks applied to a model forecast. They are NOT feature attribution, SHAP values, model confidence, or an explanation of why gradient boosting predicted a particular number. The logger records all supported decisions, including holds/rejections. `would_water` is not a command acknowledgement or proof of watering. The false `actuator_command_sent_at_policy_stage` field only describes this stage and is not evidence about later command delivery. Status/command acknowledgements must be logged separately if an end-to-end actuator audit is needed.

In Grafana add a Table panel using `grafana_decision_history.flux`. Organize fields after its final group() and show time, model, horizon, decision, predicted/current moisture, threshold, and explanation. Use a 1-minute refresh. This query uses dashboard time range; start recording now because older decisions cannot be recovered with their original evaluation context. Decision code can also supply a State timeline: 1 yellow (Would water), 0 green (Hold), -1 red (Rejected). Never label it Pump ON/OFF.

Run `node test_decision_log.js` to check formatting, reasoning text, invalid input handling, timestamp alignment, and that logging preserves the control message.

This Python service publishes forecasts only. The updated ESP32 sketch and separate Node-RED command Function now implement the predictive command path, but require firmware upload and explicit enablement as described below. Running Python alone does not enable automatic irrigation.

For authenticated brokers, set `MQTT_USERNAME` and `MQTT_PASSWORD` in the environment. Use `--tls --port 8883` when the broker supports TLS. Never commit passwords. Only load your own trusted `model.joblib` files. Keep the dependency environment used for training when serving a saved model.

API references: [scikit-learn time series splitting](https://scikit-learn.org/stable/modules/generated/sklearn.model_selection.TimeSeriesSplit.html), [Paho MQTT Python](https://eclipse.dev/paho/clients/python/docs/).
