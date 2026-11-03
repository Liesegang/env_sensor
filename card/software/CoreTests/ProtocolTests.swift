import Foundation
import Testing
@testable import EnvSensorCore

private struct Capture: Decodable {
    let at: Double
    let event: String
    let hex: String?
}

func capturedPackets() throws -> [(String, Data, Date)] {
    let url = try #require(Bundle.module.url(forResource: "card-production", withExtension: "jsonl", subdirectory: "Fixtures"))
    return try String(contentsOf: url, encoding: .utf8).split(separator: "\n").compactMap { line in
        let record = try JSONDecoder().decode(Capture.self, from: Data(line.utf8))
        guard let hex = record.hex else { return nil }
        let characters = Array(hex)
        let bytes = stride(from: 0, to: characters.count, by: 2).map {
            UInt8(String(characters[$0...($0 + 1)]), radix: 16)!
        }
        return (record.event, Data(bytes), Date(timeIntervalSince1970: record.at))
    }
}

extension Data {
    mutating func writeLE<T: FixedWidthInteger>(_ value: T, at offset: Int) {
        for index in 0..<MemoryLayout<T>.size {
            self[offset + index] = UInt8(truncatingIfNeeded: value >> (index * 8))
        }
    }
}

func fixtureReading(sequence: UInt32 = 1, uptime: UInt64 = 5_000,
                    date: Date = Date(timeIntervalSince1970: 1_700_000_000)) throws -> SensorReading {
    var bytes = try #require(capturedPackets().first(where: { $0.0 == "read" })?.1)
    bytes.writeLE(sequence, at: 12)
    bytes.writeLE(uptime, at: 16)
    return try EnvSensorProtocol.decode(bytes, receivedAt: date)
}

func fragment(_ frame: Data, sequence: UInt32? = nil, offset: Int = 0,
              count: Int? = nil, total: Int? = nil) -> Data {
    var bytes = Data([0x4f, 0x46, 1, 0] + Array(repeating: UInt8(0), count: 8))
    let sequence = sequence ?? UInt32(frame[12]) | UInt32(frame[13]) << 8 | UInt32(frame[14]) << 16 | UInt32(frame[15]) << 24
    bytes.writeLE(sequence, at: 4)
    bytes.writeLE(UInt16(total ?? frame.count), at: 8)
    bytes.writeLE(UInt16(offset), at: 10)
    bytes.append(frame.subdata(in: offset..<(offset + (count ?? frame.count - offset))))
    return bytes
}

struct ProtocolTests {
    @Test func clockSyncUsesOutdoorOTFormatAndRejectsUnsupportedDates() throws {
        let packet = try EnvSensorProtocol.encodeClockSync(unixMilliseconds: 1_791_114_000_123)
        #expect(packet == Data([0x4f, 0x54, 1, 0, 0xfb, 0xb6, 0xb6, 0x06, 0xa1, 0x01, 0, 0]))
        #expect(throws: SensorProtocolError.invalidClock) {
            try EnvSensorProtocol.encodeClockSync(unixMilliseconds: 1_577_836_800_000 - 1)
        }
        #expect(throws: SensorProtocolError.invalidClock) {
            try EnvSensorProtocol.encodeClockSync(unixMilliseconds: 4_102_444_800_000)
        }
    }
    @Test func actualCardReadAndNotificationUseCommonProtocol() throws {
        let packets = try capturedPackets()
        let assembler = FragmentAssembler()
        let decoded = try packets.map { event, data, date in
            if event == "read" { return try EnvSensorProtocol.decode(data, receivedAt: date) }
            return try #require(try assembler.push(data, receivedAt: date))
        }
        #expect(decoded.count == 3)
        #expect(decoded[0].rawFrame == decoded[1].rawFrame)
        #expect(decoded[0].sequence == 1)
        #expect(decoded[2].sequence == 2)
        #expect(decoded[2].uptimeMilliseconds - decoded[0].uptimeMilliseconds == 60_000)
        #expect(decoded[0].enabledMask == 0x4f)
        #expect(decoded[0].validMask == 0x0f)
        #expect(decoded[0].sensors["sfa40"]?.error == -61)
        #expect(decoded[0].sensors["sgp41"]?.values["voc_index_valid"] == .boolean(false))
        #expect(decoded[0].sensors["sgp41"]?.values["nox_index_valid"] == .boolean(false))
        #expect(decoded[0].sensors["sgp41"]?.values["raw_voc"] == .integer(28_040))
        let temperature = try #require(decoded[0].value(sensor: "stcc4", field: "temperature_mc", scale: 0.001))
        #expect(abs(temperature - 28.118) < 0.000_001)
        #expect(decoded[0].sensors["stcc4"]?.values["serial_number"] == .text("0x24716d163cba7b9c"))
    }

    @Test func futureUnknownTLVIsSkippedAndRawFrameKept() throws {
        var bytes = try fixtureReading().rawFrame
        bytes.append(contentsOf: [0xfe, 99, 4, 0, 0, 0, 0xde, 0xad])
        bytes.writeLE(UInt16(bytes.count), at: 4)
        let reading = try EnvSensorProtocol.decode(bytes)
        #expect(reading.sensors.count == 5)
        #expect(reading.rawFrame.suffix(2) == Data([0xde, 0xad]))
        // Unknown IDs must still obey global duplicate and block-boundary rules.
        bytes.append(contentsOf: [0xfe, 1, 2, 0, 0, 0])
        bytes.writeLE(UInt16(bytes.count), at: 4)
        #expect(throws: SensorProtocolError.duplicateSensor) { try EnvSensorProtocol.decode(bytes) }
    }

    @Test func corruptHeadersFlagsLengthsAndSensorBooleansAreRejected() throws {
        let original = try fixtureReading().rawFrame
        for (offset, value) in [(0, UInt8(0)), (2, 2), (3, 23), (4, 0), (10, 1), (25, 2)] {
            var bytes = original
            bytes[offset] = value
            #expect(throws: (any Error).self) { try EnvSensorProtocol.decode(bytes) }
        }
        var invalid = original
        invalid.writeLE(UInt16(0xff), at: 8)
        #expect(throws: SensorProtocolError.invalidFlags) { try EnvSensorProtocol.decode(invalid) }
        invalid = original
        invalid[72] = 2 // BME690 gas_valid.
        #expect(throws: SensorProtocolError.invalidBoolean) { try EnvSensorProtocol.decode(invalid) }
        invalid = original
        invalid[28] = 1 // Positive errno is forbidden.
        #expect(throws: SensorProtocolError.invalidFlags) { try EnvSensorProtocol.decode(invalid) }
        invalid = original
        invalid[26] = 13 // OPT4001 size mismatch.
        #expect(throws: SensorProtocolError.invalidSensor(1)) { try EnvSensorProtocol.decode(invalid) }
        invalid = original
        invalid.append(contentsOf: original[24..<42])
        invalid.writeLE(UInt16(invalid.count), at: 4)
        #expect(throws: SensorProtocolError.duplicateSensor) { try EnvSensorProtocol.decode(invalid) }
        invalid = original.dropLast(6)
        invalid.writeLE(UInt16(invalid.count), at: 4)
        #expect(throws: SensorProtocolError.missingSensor(7)) { try EnvSensorProtocol.decode(invalid) }
        #expect(throws: SensorProtocolError.invalidLength) { try EnvSensorProtocol.decode(Data(repeating: 0, count: 513)) }
        #expect(throws: SensorProtocolError.invalidLength) { try EnvSensorProtocol.decode(Data([0x4f])) }
    }

    @Test func fragmentedPacketAcceptsMatchingDuplicateAndRejectsGapsConflicts() throws {
        let frame = try fixtureReading().rawFrame
        let assembler = FragmentAssembler()
        #expect(try assembler.push(fragment(frame, count: 30), now: 0) == nil)
        #expect(try assembler.push(fragment(frame, count: 30), now: 0.2) == nil)
        #expect(try assembler.push(fragment(frame, offset: 30, count: 50), now: 0.4) == nil)
        #expect(try assembler.push(fragment(frame, offset: 80), now: 0.6)?.rawFrame == frame)
        assembler.reset()
        #expect(try assembler.push(fragment(frame, count: 30), now: 1) == nil)
        var conflict = fragment(frame, count: 30)
        conflict[20] ^= 1
        #expect(throws: SensorProtocolError.fragmentConflict) { try assembler.push(conflict, now: 1.2) }
        #expect(throws: SensorProtocolError.fragmentMissing) { try assembler.push(fragment(frame, offset: 30), now: 1.4) }
        #expect(try assembler.push(fragment(frame, count: 30), now: 2) == nil)
        #expect(throws: SensorProtocolError.fragmentMissing) { try assembler.push(fragment(frame, offset: 40), now: 2.2) }
        #expect(try assembler.push(fragment(frame, count: 30), now: 3) == nil)
        #expect(throws: SensorProtocolError.fragmentConflict) { try assembler.push(fragment(frame, offset: 30, total: 147), now: 3.2) }
        #expect(try assembler.push(fragment(frame, count: 30), now: 4) == nil)
        #expect(throws: SensorProtocolError.fragmentTimeout) { try assembler.push(fragment(frame, offset: 30), now: 7.01) }
        #expect(try assembler.push(fragment(frame), now: 8)?.rawFrame == frame)
    }

    @Test func newSequenceAndDisconnectDropIncompleteFrame() throws {
        let first = try fixtureReading().rawFrame
        let second = try fixtureReading(sequence: 2, uptime: 65_000).rawFrame
        let assembler = FragmentAssembler()
        #expect(try assembler.push(fragment(first, count: 30), now: 0) == nil)
        #expect(try assembler.push(fragment(second, count: 50), now: 1) == nil)
        #expect(try assembler.push(fragment(second, offset: 50), now: 1.1)?.sequence == 2)
        #expect(try assembler.push(fragment(first, count: 30), now: 2) == nil)
        assembler.reset()
        #expect(throws: SensorProtocolError.fragmentMissing) { try assembler.push(fragment(first, offset: 30), now: 2.1) }
        #expect(throws: SensorProtocolError.sequenceMismatch) { try assembler.push(fragment(first, sequence: 99), now: 3) }
        #expect(try assembler.push(fragment(first), now: 3.1)?.sequence == 1)
    }

    @Test func allSevenSensorFormatsPreserveSignedAndWideValues() throws {
        var frame = Data([0x4f, 0x45, 1, 24] + Array(repeating: UInt8(0), count: 20))
        frame.writeLE(UInt16(0x7f), at: 6)
        frame.writeLE(UInt16(0x7f), at: 8)
        frame.writeLE(UInt32.max, at: 12)
        frame.writeLE(UInt64.max, at: 16)
        for sensor in SensorSchema.sensors {
            let size = sensor.fields.reduce(2) { $0 + $1.type.width }
            frame.append(contentsOf: [sensor.id, sensor.version, UInt8(size), 0, 0, 0])
            for field in sensor.fields {
                switch field.type {
                case .u8: frame.append(255)
                case .bool: frame.append(1)
                case .u16: frame.append(contentsOf: [255, 255])
                case .i16: frame.append(contentsOf: [0xfe, 0xff])
                case .u32: frame.append(contentsOf: [255, 255, 255, 255])
                case .i32: frame.append(contentsOf: [0xfd, 0xff, 0xff, 0xff])
                case .u64: frame.append(contentsOf: Array(repeating: 255, count: 8))
                }
            }
        }
        frame.writeLE(UInt16(frame.count), at: 4)
        let reading = try EnvSensorProtocol.decode(frame)
        #expect(reading.sensors.count == 7)
        #expect(reading.sequence == UInt32.max)
        #expect(reading.uptimeMilliseconds == UInt64.max)
        #expect(reading.sensors["as3935"]?.values["distance_km"] == .integer(-2))
        #expect(reading.sensors["sfa40"]?.values["hcho_millippb"] == .integer(-3))
        #expect(reading.sensors["bmv080"]?.values["runtime_ms"] == .text("18446744073709551615"))
        #expect(reading.sensors["sfa40"]?.values["serial_number"] == .text("0xffffffffffffffff"))
        #expect(reading.sensors["bme690"]?.values["pressure_pa"] == .integer(4_294_967_295))
    }
}
