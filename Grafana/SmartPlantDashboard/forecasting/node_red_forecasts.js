// Paste into a Node-RED Function node connected to an influxdb batch node.
// Input must be a parsed JSON object from the prediction MQTT topic.
const data = msg.payload;
const originMs = Date.parse(data?.origin_utc);
if (!data?.device || !Number.isFinite(originMs) ||
    !Array.isArray(data.model_forecasts) || data.model_forecasts.length === 0) {
    node.error("Missing model_forecasts or invalid origin; restart the updated Python service", msg);
    return null;
}

const points = [];
for (const forecast of data.model_forecasts) {
    const targetMs = Date.parse(forecast.target_time_utc);
    if (!["5m", "10m", "15m"].includes(forecast.horizon) ||
        !["persistence", "random_forest", "gradient_boosting"].includes(forecast.model) ||
        typeof forecast.moisture !== "number" || !Number.isFinite(forecast.moisture) ||
        !Number.isFinite(targetMs)) {
        node.error("Invalid model forecast", msg);
        return null;
    }
    points.push({
        measurement: "moisture_forecast",
        fields: {
            predicted_moisture: forecast.moisture,
            origin_epoch_ms: originMs
        },
        tags: {
            device: String(data.device),
            horizon: forecast.horizon,
            model: forecast.model
        },
        timestamp: new Date(targetMs)
    });
}
msg.payload = points;
return msg;
