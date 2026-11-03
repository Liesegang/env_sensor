import SwiftUI

enum SensorMetric: String, CaseIterable, Identifiable, Sendable {
    case temperature, humidity, co2, illuminance, pressure, voc, nox, formaldehyde
    var id: Self { self }

    var title: String {
        switch self {
        case .temperature: "温度"
        case .humidity: "湿度"
        case .co2: "CO₂"
        case .illuminance: "照度"
        case .pressure: "気圧"
        case .voc: "VOC raw"
        case .nox: "NOx raw"
        case .formaldehyde: "ホルムアルデヒド"
        }
    }
    var unit: String {
        switch self {
        case .temperature: "°C"
        case .humidity: "%"
        case .co2: "ppm"
        case .illuminance: "lx"
        case .pressure: "hPa"
        case .voc, .nox: "ticks"
        case .formaldehyde: "ppm"
        }
    }
    var systemImage: String {
        switch self {
        case .temperature: "thermometer.medium"
        case .humidity: "humidity"
        case .co2: "cloud"
        case .illuminance: "sun.max"
        case .pressure: "gauge.with.dots.needle.50percent"
        case .voc: "aqi.medium"
        case .nox: "wind"
        case .formaldehyde: "flask"
        }
    }
    var color: Color {
        switch self {
        case .temperature: .orange
        case .humidity: .blue
        case .co2: .teal
        case .illuminance: .yellow
        case .pressure: .indigo
        case .voc: .purple
        case .nox: .mint
        case .formaldehyde: .pink
        }
    }
    func value(in reading: SensorReading) -> Double? {
        switch self {
        case .temperature:
            reading.value(sensor: "stcc4", field: "temperature_mc", scale: 0.001)
                ?? reading.value(sensor: "bme690", field: "temperature_mc", scale: 0.001)
        case .humidity:
            reading.value(sensor: "stcc4", field: "humidity_mpercent", scale: 0.001)
                ?? reading.value(sensor: "bme690", field: "humidity_mpercent", scale: 0.001)
        case .co2: reading.value(sensor: "stcc4", field: "co2_ppm")
        case .illuminance: reading.value(sensor: "opt4001", field: "millilux", scale: 0.001)
        case .pressure: reading.value(sensor: "bme690", field: "pressure_pa", scale: 0.01)
        case .voc: reading.value(sensor: "sgp41", field: "raw_voc")
        case .nox:
            reading.sensors["sgp41"]?.values["nox_valid"] == .boolean(true)
                ? reading.value(sensor: "sgp41", field: "raw_nox") : nil
        case .formaldehyde:
            reading.formaldehydePPM
        }
    }
    func formatted(_ value: Double?) -> String {
        guard let value else { return "—" }
        let decimals: Int = switch self {
        case .temperature, .humidity, .pressure: 1
        case .formaldehyde: 3
        default: 0
        }
        return value.formatted(.number.precision(.fractionLength(decimals)))
    }
}
