// Node-RED Function node: parsed MQTT prediction -> this node -> Debug.
// Decision preview only. This node never publishes a pump command.
const MODEL = "gradient_boosting";
const HORIZON = "10m";
const THRESHOLD = 60;       // Existing firmware threshold; validate experimentally.
const UPPER_LIMIT = 70;     // Provisional guard against watering already-wet soil.
const MAX_ORIGIN_AGE_MS = 90000;
const data = msg.payload;
const now = Date.now();

function finish(decision, reason, forecast) {
    msg.topic = "smartplant/policy/preview";
    msg.payload = {
        device: data && data.device,
        mode: "preview",
        model: MODEL,
        horizon: HORIZON,
        decision: decision,
        reason: reason,
        threshold: THRESHOLD,
        upper_moisture_limit: UPPER_LIMIT,
        current_moisture: data && data.current_minute_moisture,
        predicted_moisture: forecast ? forecast.moisture : null,
        origin_utc: data && data.origin_utc,
        target_time_utc: forecast ? forecast.target_time_utc : null,
        evaluated_at_utc: new Date(now).toISOString(),
        actuator_command_sent: false
    };
    node.status({fill: decision === "would_water" ? "yellow" :
                       decision === "reject" ? "red" : "green",
                 shape: "dot", text: decision + ": " + reason});
    return msg;
}

if (!data || data.device !== "esp32-01" || !Array.isArray(data.model_forecasts)) {
    return finish("reject", "invalid_payload_or_device");
}
const matches = data.model_forecasts.filter(function (f) {
    return f && f.model === MODEL && f.horizon === HORIZON;
});
if (matches.length !== 1) {
    return finish("reject", "missing_or_duplicate_gradient_forecast");
}
const forecast = matches[0];
const origin = Date.parse(data.origin_utc);
const target = Date.parse(forecast.target_time_utc);
if (!Number.isFinite(origin) || now - origin > MAX_ORIGIN_AGE_MS || origin > now) {
    return finish("reject", "stale_or_invalid_origin", forecast);
}
if (!Number.isFinite(target) || target - origin !== 10 * 60000) {
    return finish("reject", "invalid_target_time", forecast);
}
const current = data.current_minute_moisture;
const predicted = forecast.moisture;
if (typeof current !== "number" || !Number.isFinite(current) || current < 0 || current > 100 ||
    typeof predicted !== "number" || !Number.isFinite(predicted) || predicted < 0 || predicted > 100) {
    return finish("reject", "invalid_moisture", forecast);
}
if (current >= UPPER_LIMIT) {
    return finish("hold", "current_moisture_at_upper_limit", forecast);
}
if (predicted <= THRESHOLD) {
    return finish("would_water", "forecast_at_or_below_threshold", forecast);
}
return finish("hold", "forecast_above_threshold", forecast);
