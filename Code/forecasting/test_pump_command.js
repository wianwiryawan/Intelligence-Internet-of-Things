const assert = require('assert');
const fs = require('fs');
const path = require('path');
const source = fs.readFileSync(path.join(__dirname, 'node_red_pump_command.js'), 'utf8');
function harness(enabled) {
    const values = {};
    const context = {get: key => values[key], set: (key, value) => { values[key] = value; }};
    const run = new Function('msg', 'context', enabled ? source.replace(
        'const ENABLE_COMMANDS = false;', 'const ENABLE_COMMANDS = true;') : source);
    return {run: msg => run(msg, context), values: values};
}
function decision() {
    const origin = Math.floor(Date.now() / 60000) * 60000;
    return {payload: {device: 'esp32-01', mode: 'preview', decision: 'would_water',
        model: 'gradient_boosting', horizon: '10m', predicted_moisture: 58,
        current_moisture: 55, origin_utc: new Date(origin).toISOString(),
        target_time_utc: new Date(origin + 600000).toISOString()}};
}
function status() {
    return {topic: '/smartplant/esp32-01/status/control', payload: {session: 'test-session',
        mode: 'predictive', predictive_ready: true, pump_on: false, cooldown_remaining_ms: 0}};
}
const observe = harness(false);
observe.run(status());
assert.strictEqual(observe.run(decision())[1].payload.reason, 'observation_only_commands_disabled');
const active = harness(true);
assert.strictEqual(active.run(decision())[0], null);
active.run(status());
const first = active.run(decision());
assert.strictEqual(first[0].retain, false);
const cmd = JSON.parse(first[0].payload);
assert.strictEqual(cmd.model, 'gradient_boosting');
assert.strictEqual(cmd.duration_ms, 500);
assert.strictEqual(cmd.expires_at_epoch_ms - cmd.issued_at_epoch_ms, 15000);
assert.strictEqual(cmd.session, 'test-session');
assert.strictEqual(active.run(decision())[1].payload.reason, 'duplicate_or_local_cooldown');
const stale = harness(true);
stale.run(status());
stale.values.pump_status.received -= 16000;
assert.strictEqual(stale.run(decision())[0], null);
const retained = harness(true);
const retainedStatus = status(); retainedStatus.retain = true;
retained.run(retainedStatus);
assert.strictEqual(retained.run(decision())[0], null);
const unsafe = harness(true); const unsafeStatus = status();
unsafeStatus.payload.predictive_ready = false; unsafe.run(unsafeStatus);
assert.strictEqual(unsafe.run(decision())[0], null);
const invalid = decision(); invalid.payload.predicted_moisture = 80;
assert.strictEqual(active.run(invalid)[0], null);
const textStatus = harness(true);
const textMessage = status(); textMessage.payload = JSON.stringify(textMessage.payload);
assert.strictEqual(textStatus.run(textMessage)[1].payload.result, 'status_cached');
assert(textStatus.run(decision())[0]);
const badJson = status(); badJson.payload = '{broken';
assert.strictEqual(textStatus.run(badJson)[1].payload.reason, 'invalid_status_json');
const badPayload = status(); badPayload.payload = {mode: 'predictive'};
assert.strictEqual(textStatus.run(badPayload)[1].payload.reason, 'invalid_status_payload');
const wrongTopic = status(); wrongTopic.topic = '/wrong/topic';
assert.strictEqual(textStatus.run(wrongTopic)[1].payload.reason, 'unexpected_status_topic');
const emptyCache = harness(true);
assert.strictEqual(emptyCache.run(decision())[1].payload.cache_present, false);
assert(stale.run(decision())[1].payload.status_age_ms >= 16000);
console.log('19 pump-command checks passed');
