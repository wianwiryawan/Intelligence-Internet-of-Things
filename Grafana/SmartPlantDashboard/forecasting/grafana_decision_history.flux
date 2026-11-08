from(bucket: "SmartPlantIoT")
    |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
    |> filter(fn: (r) =>
        r._measurement == "moisture_policy_decision" and
        r.device == "esp32-01" and r.stage == "policy"
    )
    |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
    |> keep(columns: ["_time", "device", "model", "horizon", "decision", "reason_code",
        "explanation", "current_moisture", "predicted_moisture", "threshold",
        "upper_moisture_limit", "origin_epoch_ms", "target_epoch_ms"])
    |> group()
    |> sort(columns: ["_time"], desc: true)
    |> yield(name: "policy_history")
