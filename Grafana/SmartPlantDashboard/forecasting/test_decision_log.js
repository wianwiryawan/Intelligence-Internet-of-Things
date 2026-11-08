const assert = require('assert');
const fs = require('fs');
const path = require('path');
const policy = new Function('msg', 'node', fs.readFileSync(
    path.join(__dirname, 'node_red_gradient_policy.js'), 'utf8'));
const log = new Function('msg', 'node', fs.readFileSync(
    path.join(__dirname, 'node_red_decision_log.js'), 'utf8'));
const node = {status: function () {}, error: function () {}};
function decision(current, predicted, age) {
    const origin = Date.now() - (age || 1000);
    return policy({payload: {device: 'esp32-01', current_minute_moisture: current,
        origin_utc: new Date(origin).toISOString(), model_forecasts: [
            {model: 'gradient_boosting', horizon: '10m', moisture: predicted,
             target_time_utc: new Date(origin + 600000).toISOString()}]}}, node);
}
const input = decision(55, 58);
const before = JSON.stringify(input);
const point = log(input, node).payload[0];
assert.strictEqual(JSON.stringify(input), before); // Logging cannot rewrite controller input.
assert.strictEqual(point.measurement, 'moisture_policy_decision');
assert.strictEqual(point.fields.decision, 'would_water');
assert.strictEqual(point.fields.decision_code, 1);
assert(point.fields.explanation.includes('58.00% <= threshold 60.00%'));
assert.strictEqual(point.fields.predicted_moisture, 58);
assert.strictEqual(point.fields.actuator_command_sent_at_policy_stage, false);
assert.strictEqual(point.timestamp.toISOString(), input.payload.evaluated_at_utc);
assert.strictEqual(point.tags.model, 'gradient_boosting');
assert.strictEqual(log(decision(55, 65), node).payload[0].fields.decision_code, 0);
assert.strictEqual(log(decision(70, 58), node).payload[0].fields.reason_code,
    'current_moisture_at_upper_limit');
assert.strictEqual(log(decision(55, 58, 120000), node).payload[0].fields.decision_code, -1);
const bad = log(decision(55, NaN), node).payload[0];
assert(!Object.prototype.hasOwnProperty.call(bad.fields, 'predicted_moisture'));
assert.strictEqual(bad.fields.reason_code, 'invalid_moisture');
assert.strictEqual(log({payload: {result: 'accepted'}}, node), null);
assert.strictEqual(log({payload: {mode: 'preview', decision: 'hold', reason: 'test'}}, node), null);
console.log('16 decision logging checks passed');
