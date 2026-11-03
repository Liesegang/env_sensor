// Generated from shared/protocol/env-sensor-v1.json. Keep wire definitions in sync.
import Foundation

public struct SensorFieldDefinition: Sendable {
    public let key: String
    public let label: String
    public let scale: Double
    public let unit: String?
    let type: WireType
    let hexadecimal: Bool
}

public struct SensorDefinition: Sendable {
    public let id: UInt8
    public let key: String
    public let name: String
    public let version: UInt8
    public let fields: [SensorFieldDefinition]
}

enum WireType: Sendable {
    case u8, u16, i16, u32, i32, u64, bool
    var width: Int {
        switch self {
        case .u8, .bool: 1
        case .u16, .i16: 2
        case .u32, .i32: 4
        case .u64: 8
        }
    }
}

public enum SensorSchema {
    public static let serviceUUID = "722acd80-e77f-468d-a916-67e599e7cffd"
    public static let characteristicUUID = "722acd81-e77f-468d-a916-67e599e7cffd"
    public static let sensors: [SensorDefinition] = [
        SensorDefinition(id: 1, key: "opt4001", name: "OPT4001", version: 1, fields: [
            SensorFieldDefinition(key: "millilux", label: "照度", scale: 0.001, unit: "lx", type: .u32, hexadecimal: false),
            SensorFieldDefinition(key: "mantissa", label: "Mantissa", scale: 1.0, unit: nil, type: .u32, hexadecimal: false),
            SensorFieldDefinition(key: "device_id", label: "Device ID", scale: 1.0, unit: nil, type: .u16, hexadecimal: true),
            SensorFieldDefinition(key: "exponent", label: "Exponent", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "counter", label: "Counter", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
        ]),
        SensorDefinition(id: 2, key: "bme690", name: "BME690", version: 1, fields: [
            SensorFieldDefinition(key: "temperature_mc", label: "温度", scale: 0.001, unit: "°C", type: .i32, hexadecimal: false),
            SensorFieldDefinition(key: "pressure_pa", label: "気圧", scale: 0.01, unit: "hPa", type: .u32, hexadecimal: false),
            SensorFieldDefinition(key: "humidity_mpercent", label: "湿度", scale: 0.001, unit: "%RH", type: .u32, hexadecimal: false),
            SensorFieldDefinition(key: "gas_resistance_ohm", label: "ガス抵抗", scale: 1.0, unit: "Ω", type: .u32, hexadecimal: false),
            SensorFieldDefinition(key: "chip_id", label: "Chip ID", scale: 1.0, unit: nil, type: .u8, hexadecimal: true),
            SensorFieldDefinition(key: "variant_id", label: "Variant ID", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "status", label: "Status", scale: 1.0, unit: nil, type: .u8, hexadecimal: true),
            SensorFieldDefinition(key: "measurement_index", label: "Measurement index", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "gas_index", label: "Gas index", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "heater_resistance", label: "Heater resistance", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "heater_current", label: "Heater current", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "gas_wait", label: "Gas wait", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "gas_valid", label: "Gas valid", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "heater_stable", label: "Heater stable", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
        ]),
        SensorDefinition(id: 3, key: "sgp41", name: "SGP41", version: 2, fields: [
            SensorFieldDefinition(key: "serial_number", label: "Serial", scale: 1.0, unit: nil, type: .u64, hexadecimal: true),
            SensorFieldDefinition(key: "raw_voc", label: "VOC raw", scale: 1.0, unit: nil, type: .u16, hexadecimal: false),
            SensorFieldDefinition(key: "raw_nox", label: "NOx raw", scale: 1.0, unit: nil, type: .u16, hexadecimal: false),
            SensorFieldDefinition(key: "self_test", label: "Self test", scale: 1.0, unit: nil, type: .u16, hexadecimal: true),
            SensorFieldDefinition(key: "temperature_ticks", label: "補償温度 ticks", scale: 1.0, unit: nil, type: .u16, hexadecimal: false),
            SensorFieldDefinition(key: "humidity_ticks", label: "補償湿度 ticks", scale: 1.0, unit: nil, type: .u16, hexadecimal: false),
            SensorFieldDefinition(key: "nox_valid", label: "NOx valid", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "conditioning", label: "Conditioning", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "voc_index", label: "VOC Index", scale: 1.0, unit: "Index", type: .u16, hexadecimal: false),
            SensorFieldDefinition(key: "nox_index", label: "NOx Index", scale: 1.0, unit: "Index", type: .u16, hexadecimal: false),
            SensorFieldDefinition(key: "voc_index_valid", label: "VOC Index valid", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "nox_index_valid", label: "NOx Index valid", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
        ]),
        SensorDefinition(id: 4, key: "stcc4", name: "STCC4", version: 1, fields: [
            SensorFieldDefinition(key: "serial_number", label: "Serial", scale: 1.0, unit: nil, type: .u64, hexadecimal: true),
            SensorFieldDefinition(key: "product_id", label: "Product ID", scale: 1.0, unit: nil, type: .u32, hexadecimal: true),
            SensorFieldDefinition(key: "temperature_mc", label: "SHT45 温度", scale: 0.001, unit: "°C", type: .i32, hexadecimal: false),
            SensorFieldDefinition(key: "humidity_mpercent", label: "SHT45 湿度", scale: 0.001, unit: "%RH", type: .i32, hexadecimal: false),
            SensorFieldDefinition(key: "co2_ppm", label: "CO₂", scale: 1.0, unit: "ppm", type: .i16, hexadecimal: false),
            SensorFieldDefinition(key: "raw_temperature", label: "SHT45 raw temperature", scale: 1.0, unit: nil, type: .u16, hexadecimal: false),
            SensorFieldDefinition(key: "raw_humidity", label: "SHT45 raw humidity", scale: 1.0, unit: nil, type: .u16, hexadecimal: false),
            SensorFieldDefinition(key: "status", label: "Status", scale: 1.0, unit: nil, type: .u16, hexadecimal: true),
        ]),
        SensorDefinition(id: 5, key: "bmv080", name: "BMV080", version: 1, fields: [
            SensorFieldDefinition(key: "pm1_mass_milliug_m3", label: "PM1", scale: 0.001, unit: "µg/m³", type: .i32, hexadecimal: false),
            SensorFieldDefinition(key: "pm2_5_mass_milliug_m3", label: "PM2.5", scale: 0.001, unit: "µg/m³", type: .i32, hexadecimal: false),
            SensorFieldDefinition(key: "pm10_mass_milliug_m3", label: "PM10", scale: 0.001, unit: "µg/m³", type: .i32, hexadecimal: false),
            SensorFieldDefinition(key: "pm1_number_milliparticles_cm3", label: "PM1 粒子数", scale: 0.001, unit: "particles/cm³", type: .i32, hexadecimal: false),
            SensorFieldDefinition(key: "pm2_5_number_milliparticles_cm3", label: "PM2.5 粒子数", scale: 0.001, unit: "particles/cm³", type: .i32, hexadecimal: false),
            SensorFieldDefinition(key: "pm10_number_milliparticles_cm3", label: "PM10 粒子数", scale: 0.001, unit: "particles/cm³", type: .i32, hexadecimal: false),
            SensorFieldDefinition(key: "runtime_ms", label: "Runtime", scale: 1.0, unit: "ms", type: .u64, hexadecimal: false),
            SensorFieldDefinition(key: "sequence", label: "Sequence", scale: 1.0, unit: nil, type: .u32, hexadecimal: false),
            SensorFieldDefinition(key: "age_ms", label: "Age", scale: 1.0, unit: "ms", type: .u32, hexadecimal: false),
            SensorFieldDefinition(key: "is_obstructed", label: "Obstructed", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "is_outside_measurement_range", label: "Outside range", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "fresh", label: "Fresh", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
        ]),
        SensorDefinition(id: 6, key: "as3935", name: "AS3935", version: 1, fields: [
            SensorFieldDefinition(key: "energy", label: "雷エネルギー", scale: 1.0, unit: nil, type: .u32, hexadecimal: false),
            SensorFieldDefinition(key: "distance_km", label: "推定距離", scale: 1.0, unit: "km", type: .i16, hexadecimal: false),
            SensorFieldDefinition(key: "interrupt_reason", label: "IRQ reason", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "distance_code", label: "Distance code", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "noise_floor", label: "Noise floor", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "watchdog_threshold", label: "Watchdog", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "spike_rejection", label: "Spike rejection", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "minimum_lightnings", label: "Minimum lightnings", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "tuning_capacitor", label: "Tuning capacitor", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "trco_calibration", label: "TRCO calibration", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "srco_calibration", label: "SRCO calibration", scale: 1.0, unit: nil, type: .u8, hexadecimal: false),
            SensorFieldDefinition(key: "lightning", label: "雷", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "noise_high", label: "過大ノイズ", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "disturber", label: "妨害信号", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "storm_overhead", label: "頭上", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "out_of_range", label: "範囲外", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "distance_valid", label: "Distance valid", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "indoor", label: "Indoor", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "powered_down", label: "Powered down", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "disturbers_masked", label: "Mask disturbers", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
        ]),
        SensorDefinition(id: 7, key: "sfa40", name: "SFA40", version: 1, fields: [
            SensorFieldDefinition(key: "serial_number", label: "Serial", scale: 1.0, unit: nil, type: .u64, hexadecimal: true),
            SensorFieldDefinition(key: "product_id", label: "Product ID", scale: 1.0, unit: nil, type: .u32, hexadecimal: true),
            SensorFieldDefinition(key: "hcho_millippb", label: "ホルムアルデヒド", scale: 0.001, unit: "ppb", type: .i32, hexadecimal: false),
            SensorFieldDefinition(key: "humidity_mpercent", label: "湿度", scale: 0.001, unit: "%RH", type: .i32, hexadecimal: false),
            SensorFieldDefinition(key: "temperature_mc", label: "温度", scale: 0.001, unit: "°C", type: .i32, hexadecimal: false),
            SensorFieldDefinition(key: "raw_hcho", label: "HCHO raw", scale: 1.0, unit: nil, type: .u16, hexadecimal: false),
            SensorFieldDefinition(key: "raw_humidity", label: "Humidity raw", scale: 1.0, unit: nil, type: .u16, hexadecimal: false),
            SensorFieldDefinition(key: "raw_temperature", label: "Temperature raw", scale: 1.0, unit: nil, type: .u16, hexadecimal: false),
            SensorFieldDefinition(key: "status", label: "Status", scale: 1.0, unit: nil, type: .u8, hexadecimal: true),
            SensorFieldDefinition(key: "ready", label: "Ready", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
            SensorFieldDefinition(key: "within_specification", label: "Within specification", scale: 1.0, unit: nil, type: .bool, hexadecimal: false),
        ]),
    ]
}
