// Node-RED Function: branch from Gradient Boosting Policy Preview -> this -> influxdb batch.
// One output. Returns a NEW message; does not modify the control-policy message.
const data = msg.payload;
if (!data || data.mode !== "preview" ||
    !["would_water", "hold", "reject"].includes(data.decision) ||
    typeof data.reason !== "string") {
    node.error("Decision logger requires a policy-preview object", msg);
    return null;
}
const evaluated = Date.parse(data.evaluated_at_utc);
if (!Number.isFinite(evaluated)) {
    node.error("Decision has no valid evaluation time", msg);
    return null;
}
function finite(value) {
    return typeof value === "number" && Number.isFinite(value);
}
function display(value) {
    return finite(value) ? value.toFixed(2) : "unavailable";
}
const explanations = {
    forecast_at_or_below_threshold:
        "Forecast " + display(data.predicted_moisture) + "% <= threshold " +
        display(data.threshold) + "%; current minute-average " + display(data.current_moisture) +
        "% < upper limit " + display(data.upper_moisture_limit) +
        "%. Policy qualifies for a watering request; actuator checks are separate.",
    forecast_above_threshold:
        "Forecast " + display(data.predicted_moisture) + "% > threshold " +
        display(data.threshold) + "%. Policy holds watering.",
    current_moisture_at_upper_limit:
        "Current minute-average " + display(data.current_moisture) + "% >= upper limit " +
        display(data.upper_moisture_limit) + "%. Policy holds watering.",
    invalid_payload_or_device: "Payload is invalid or is not for the configured device.",
    missing_or_duplicate_gradient_forecast:
        "Exactly one Gradient Boosting forecast for the selected horizon is required.",
    stale_or_invalid_origin:
        "Forecast origin is invalid, in the future, or older than the policy's 90-second limit.",
    invalid_target_time: "Target time is invalid or is not exactly 10 minutes after origin.",
    invalid_moisture: "Current or predicted moisture is not a finite number in the range 0 to 100%."
};
const fields = {
    decision: data.decision,
    reason_code: data.reason,
    explanation: explanations[data.reason] || "Unrecognized policy reason: " + data.reason,
    explanation_type: "control_policy_rules",
    policy_version: "gradient_10m_v1",
    control_mode: "preview",
    // This records the POLICY stage; it does not describe later command delivery.
    actuator_command_sent_at_policy_stage: false
};
for (const name of ["current_moisture", "predicted_moisture", "threshold", "upper_moisture_limit"]) {
    if (finite(data[name])) fields[name] = data[name];
}
for (const pair of [["origin_utc", "origin_epoch_ms"], ["target_time_utc", "target_epoch_ms"]]) {
    const value = Date.parse(data[pair[0]]);
    if (Number.isFinite(value)) fields[pair[1]] = value;
}
// Numeric value for a Grafana State timeline or count queries.
fields.decision_code = {would_water: 1, hold: 0, reject: -1}[data.decision];
return {payload: [{
    measurement: "moisture_policy_decision",
    tags: {
        device: typeof data.device === "string" && data.device ? data.device : "unknown",
        model: data.model || "unknown",
        horizon: data.horizon || "unknown",
        stage: "policy"
    },
    fields: fields,
    // Decision time, NOT forecast target time: future actions have not happened yet.
    timestamp: new Date(evaluated)
}]};
