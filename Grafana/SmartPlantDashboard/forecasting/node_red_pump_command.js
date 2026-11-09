// One Function node, TWO outputs: 1 -> MQTT out, 2 -> Debug.
// Inputs: policy-preview messages AND parsed status/control messages.
// Start in observation mode. Change to true only after firmware upload/testing.
const ENABLE_COMMANDS = false;
const STATUS_TOPIC = "/smartplant/esp32-01/status/control";
const COMMAND_TOPIC = "/smartplant/esp32-01/control/pump";
const now = Date.now();
let data = msg.payload;

if (msg.topic === STATUS_TOPIC) {
    if (msg.retain) return [null, {payload: {result: "blocked", reason: "retained_status"}}];
    // Accept either parsed objects or JSON text from MQTT/JSON nodes.
    // This avoids silently dropping a status that was stringified upstream.
    if (typeof Buffer !== "undefined" && Buffer.isBuffer(data)) data = data.toString("utf8");
    if (typeof data === "string") {
        try { data = JSON.parse(data); }
        catch (error) {
            return [null, {payload: {result: "blocked", reason: "invalid_status_json"}}];
        }
    }
    if (!data || typeof data !== "object" || Array.isArray(data) ||
        typeof data.session !== "string" || !data.session) {
        return [null, {payload: {result: "blocked", reason: "invalid_status_payload",
            payload_type: typeof data, expected: "object with non-empty session"}}];
    }
    context.set("pump_status", {received: now, data: data});
    return [null, {payload: {result: "status_cached", topic: msg.topic,
        mode: data.mode, predictive_ready: data.predictive_ready,
        received_at_utc: new Date(now).toISOString()}}];
}

function blocked(reason) {
    return [null, {payload: {result: "blocked", reason: reason,
        evaluated_at_utc: new Date(now).toISOString()}}];
}
// A status-shaped object arriving under another topic cannot update the cache.
if (data && typeof data === "object" && typeof data.session === "string") {
    return [null, {payload: {result: "blocked", reason: "unexpected_status_topic",
        received_topic: msg.topic || "(missing)", expected_topic: STATUS_TOPIC}}];
}
if (!data || data.device !== "esp32-01" || data.mode !== "preview"
    || data.model !== "gradient_boosting" || data.horizon !== "10m"
    || data.decision !== "would_water") return blocked("no_qualifying_decision");
const origin = Date.parse(data.origin_utc);
const target = Date.parse(data.target_time_utc);
if (!Number.isFinite(origin) || origin > now || now - origin > 90000
    || target - origin !== 600000) return blocked("stale_or_invalid_forecast");
if (typeof data.predicted_moisture !== "number" || !Number.isFinite(data.predicted_moisture)
    || data.predicted_moisture < 0 || data.predicted_moisture > 60
    || typeof data.current_moisture !== "number" || !Number.isFinite(data.current_moisture)
    || data.current_moisture < 0 || data.current_moisture >= 70) return blocked("invalid_moisture");
const cached = context.get("pump_status");
if (!cached || now - cached.received > 15000 || cached.received > now) {
    return [null, {payload: {result: "blocked", reason: "waiting_for_fresh_esp32_status",
        cache_present: Boolean(cached), status_age_ms: cached ? now - cached.received : null,
        evaluated_at_utc: new Date(now).toISOString()}}];
}
const status = cached.data;
if (status.mode !== "predictive" || status.predictive_ready !== true
    || status.pump_on !== false || status.cooldown_remaining_ms !== 0)
    return blocked("esp32_not_ready");
if (!ENABLE_COMMANDS) return blocked("observation_only_commands_disabled");
const previous = context.get("last_pump_request");
if (previous && (origin <= previous.origin || now - previous.sent < 300000))
    return blocked("duplicate_or_local_cooldown");
const command = {
    command_id: status.session + "-" + origin,
    session: status.session,
    action: "pulse",
    duration_ms: 500,
    issued_at_epoch_ms: now,
    expires_at_epoch_ms: now + 15000,
    origin_epoch_ms: origin,
    model: "gradient_boosting",
    horizon: "10m",
    predicted_moisture: data.predicted_moisture
};
// Conservative: even a lost/rejected request starts the local cooldown.
// This is a request, NOT confirmation that watering occurred.
context.set("last_pump_request", {origin: origin, sent: now});
return [{topic: COMMAND_TOPIC, payload: JSON.stringify(command), qos: "0", retain: false},
    {payload: {result: "request_sent", command: command}}];
