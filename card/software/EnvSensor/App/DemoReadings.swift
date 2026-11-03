import Foundation

/// Isolated, explicitly labelled simulator data. Never used by a Release build.
enum DemoReadings {
    static func populate(_ store: SensorStore) throws {
        guard try store.counts().sampleCount == 0 else { return }
        let deviceID = "00000000-0000-0000-0000-000000000001"
        try store.registerDevice(id: deviceID, name: "Card Sensor", enabled: true)
        let end = Date()
        for index in 0..<1440 {
            let phase = Double(index) / 1440 * .pi * 2
            let temperature = Int64((23.4 + sin(phase) * 1.6) * 1000)
            let humidity = Int64((47.2 + cos(phase + 0.8) * 5.2) * 1000)
            let sensors: [String: SensorBlock] = [
                "stcc4": SensorBlock(id: 4, error: 0, values: [
                    "temperature_mc": .integer(temperature), "humidity_mpercent": .integer(humidity),
                    "co2_ppm": .integer(Int64(540 + sin(phase * 3) * 90)), "status": .integer(0)]),
                "bme690": SensorBlock(id: 2, error: 0, values: ["pressure_pa": .integer(Int64(101400 + sin(phase) * 210))]),
                "opt4001": SensorBlock(id: 1, error: 0, values: ["millilux": .integer(Int64(max(0, 240 + sin(phase - .pi / 2) * 230) * 1000))]),
                "sgp41": SensorBlock(id: 3, error: 0, values: ["raw_voc": .integer(Int64(28000 + sin(phase * 4) * 450)),
                    "raw_nox": .integer(Int64(17400 + cos(phase * 3) * 85)), "nox_valid": .boolean(true),
                    "voc_index_valid": .boolean(false), "nox_index_valid": .boolean(false)]),
                "sfa40": SensorBlock(id: 7, error: -61, values: [:]),
            ]
            let reading = SensorReading(sequence: UInt32(index + 1), uptimeMilliseconds: UInt64(index * 60_000),
                enabledMask: 0x4F, validMask: 0x0F, sensors: sensors,
                receivedAt: end.addingTimeInterval(Double(index - 1439) * 60), rawFrame: Data())
            try store.save(reading, deviceID: deviceID)
        }
    }
}
