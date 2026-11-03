import Foundation

/// Lossless wire values. The protocol's 64-bit values use strings in JSON,
/// matching the outdoor receiver and avoiding JavaScript integer precision loss.
public enum SensorValue: Codable, Sendable, Equatable {
    case integer(Int64)
    case boolean(Bool)
    case text(String)

    public init(from decoder: any Decoder) throws {
        let value = try decoder.singleValueContainer()
        if let boolean = try? value.decode(Bool.self) { self = .boolean(boolean) }
        else if let integer = try? value.decode(Int64.self) { self = .integer(integer) }
        else { self = .text(try value.decode(String.self)) }
    }

    public func encode(to encoder: any Encoder) throws {
        var value = encoder.singleValueContainer()
        switch self {
        case .integer(let integer): try value.encode(integer)
        case .boolean(let boolean): try value.encode(boolean)
        case .text(let text): try value.encode(text)
        }
    }

    public var number: Double? {
        if case .integer(let value) = self { return Double(value) }
        return nil
    }

    public var string: String {
        switch self {
        case .integer(let value): String(value)
        case .boolean(let value): value ? "true" : "false"
        case .text(let value): value
        }
    }
}

public struct SensorBlock: Codable, Sendable, Equatable {
    public let id: UInt8
    public let error: Int16
    public let values: [String: SensorValue]

    public init(id: UInt8, error: Int16, values: [String: SensorValue]) {
        self.id = id
        self.error = error
        self.values = values
    }
}

public struct SensorReading: Codable, Sendable, Equatable {
    public let sequence: UInt32
    public let uptimeMilliseconds: UInt64
    public let enabledMask: UInt16
    public let validMask: UInt16
    public let sensors: [String: SensorBlock]
    public let receivedAt: Date
    public let rawFrame: Data
    public let measuredAt: Date?
    public let measurementSource: String?

    public init(sequence: UInt32, uptimeMilliseconds: UInt64, enabledMask: UInt16,
                validMask: UInt16, sensors: [String: SensorBlock], receivedAt: Date,
                rawFrame: Data, measuredAt: Date? = nil, measurementSource: String? = nil) {
        self.sequence = sequence
        self.uptimeMilliseconds = uptimeMilliseconds
        self.enabledMask = enabledMask
        self.validMask = validMask
        self.sensors = sensors
        self.receivedAt = receivedAt
        self.rawFrame = rawFrame
        self.measuredAt = measuredAt
        self.measurementSource = measurementSource
    }

    /// Historical packets with unknown UTC must never be plotted as if they
    /// were measured when the phone eventually downloaded them.
    public var chartTimestamp: Date? {
        measuredAt ?? (["card-history-v1", "card-live-v1"].contains(measurementSource ?? "") ? nil : receivedAt)
    }

    public func withMeasuredAt(_ date: Date?, measurementSource source: String? = nil) -> Self {
        Self(sequence: sequence, uptimeMilliseconds: uptimeMilliseconds, enabledMask: enabledMask,
            validMask: validMask, sensors: sensors, receivedAt: receivedAt, rawFrame: rawFrame,
            measuredAt: date, measurementSource: source ?? measurementSource)
    }

    public func value(sensor: String, field: String, scale: Double = 1) -> Double? {
        guard let block = sensors[sensor], block.error == 0,
              let value = block.values[field]?.number else { return nil }
        return value * scale
    }

    public var formaldehydePPM: Double? {
        if measurementSource == "card-history-v1" {
            return value(sensor: "sfa40", field: "hcho_millippb", scale: 0.000001)
        }
        guard sensors["sfa40"]?.values["ready"] == .boolean(true),
              sensors["sfa40"]?.values["within_specification"] == .boolean(true) else { return nil }
        return value(sensor: "sfa40", field: "hcho_millippb", scale: 0.000001)
    }
}

public struct DeviceRecord: Codable, Sendable, Identifiable, Equatable {
    public let id: String
    public let name: String
    public let enabled: Bool
    public let firstSeen: Date
    public let lastSeen: Date
}

public struct StoredReading: Codable, Sendable, Identifiable, Equatable {
    public let id: Int64
    public let deviceID: String
    public let bootID: String
    public let reading: SensorReading
}

public struct StoreCounts: Sendable, Equatable {
    public let deviceCount: Int
    public let sampleCount: Int
    public let samplesByDevice: [String: Int]
    public var total: Int { sampleCount }
}

public struct ReadingExportBatch: Codable, Sendable {
    public let schema: String
    public let generatedAt: Date
    public let devices: [DeviceRecord]
    public let readings: [StoredReading]
    public let nextID: Int64
}
