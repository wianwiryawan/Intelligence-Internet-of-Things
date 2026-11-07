// Run: node test_gradient_policy.js
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const evaluate = new Function('msg', 'node', fs.readFileSync(
    path.join(__dirname, 'node_red_gradient_policy.js'), 'utf8'));
function payload(current, predicted, age) {
    const origin = Date.now() - (age || 1000);
    return {device: 'esp32-01', current_minute_moisture: current,
        origin_utc: new Date(origin).toISOString(), model_forecasts: [
            {model: 'persistence', horizon: '10m', moisture: 99,
             target_time_utc: new Date(origin + 600000).toISOString()},
            {model: 'gradient_boosting', horizon: '10m', moisture: predicted,
             target_time_utc: new Date(origin + 600000).toISOString()}]};
}
function result(data) {
    return evaluate({payload: data}, {status: function () {}}).payload;
}
assert.strictEqual(result(payload(55, 58)).decision, 'would_water');
assert.strictEqual(result(payload(55, 65)).decision, 'hold');
assert.strictEqual(result(payload(70, 58)).decision, 'hold');
assert.strictEqual(result(payload(55, 58, 120000)).decision, 'reject');
assert.strictEqual(result(payload(55, NaN)).decision, 'reject');
const missing = payload(55, 58);
missing.model_forecasts.pop();
assert.strictEqual(result(missing).decision, 'reject');
const invalidTarget = payload(55, 58);
invalidTarget.model_forecasts[1].target_time_utc = invalidTarget.origin_utc;
assert.strictEqual(result(invalidTarget).decision, 'reject');
assert.strictEqual(result(payload(55, 58)).actuator_command_sent, false);
console.log('8 policy checks passed');
