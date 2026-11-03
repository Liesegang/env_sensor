import Foundation

public struct CardHistoryRecord: Sendable, Equatable {
    public let sequence: UInt32
    public let uptimeSeconds: UInt32
    public let unixSeconds: UInt64
    public let raw: Data
    public let valid: UInt16
    public init(raw: Data) throws {
        let b = [UInt8](raw)
        guard b.count == GeneratedHistorySchema.Record.size, b[GeneratedHistorySchema.Record.reserved] == 0, b[GeneratedHistorySchema.Record.rhtSource] <= 2, b[GeneratedHistorySchema.Record.gasValid] <= 1, b[GeneratedHistorySchema.Record.heaterStable] <= 1 else {
            throw CardHistoryError.invalidRecord
        }
        sequence = b.historyU32(GeneratedHistorySchema.Record.sequence); uptimeSeconds = b.historyU32(GeneratedHistorySchema.Record.uptimeS); unixSeconds = b.historyU64(GeneratedHistorySchema.Record.unixS)
        valid = b.historyU16(GeneratedHistorySchema.Record.valid)
        self.raw = raw
        guard sequence != 0, valid & ~0x1fff == 0,
              unixSeconds == 0 || (GeneratedHistorySchema.utcMinimum...GeneratedHistorySchema.utcMaximum).contains(unixSeconds) else {
            throw CardHistoryError.invalidRecord
        }
        let fields: [(UInt16, Int, Int)] = [
            (GeneratedHistorySchema.Validity.temperature, GeneratedHistorySchema.Record.temperatureMc, 4),
            (GeneratedHistorySchema.Validity.humidity, GeneratedHistorySchema.Record.humidityMpercent, 4),
            (GeneratedHistorySchema.Validity.pressure, GeneratedHistorySchema.Record.pressurePa, 4),
            (GeneratedHistorySchema.Validity.co2, GeneratedHistorySchema.Record.co2Ppm, 2),
            (GeneratedHistorySchema.Validity.lux, GeneratedHistorySchema.Record.millilux, 4),
            (GeneratedHistorySchema.Validity.bmeTemperature, GeneratedHistorySchema.Record.bmeTemperatureMc, 4),
            (GeneratedHistorySchema.Validity.bmeHumidity, GeneratedHistorySchema.Record.bmeHumidityMpercent, 4),
            (GeneratedHistorySchema.Validity.bmeGas, GeneratedHistorySchema.Record.gasResistanceOhm, 4),
            (GeneratedHistorySchema.Validity.vocRaw, GeneratedHistorySchema.Record.rawVoc, 2),
            (GeneratedHistorySchema.Validity.noxRaw, GeneratedHistorySchema.Record.rawNox, 2),
            (GeneratedHistorySchema.Validity.vocIndex, GeneratedHistorySchema.Record.vocIndex, 2),
            (GeneratedHistorySchema.Validity.noxIndex, GeneratedHistorySchema.Record.noxIndex, 2),
            (GeneratedHistorySchema.Validity.hcho, GeneratedHistorySchema.Record.hchoMillippb, 4)
        ]
        guard fields.allSatisfy({ mask, offset, length in
            valid & mask != 0 || b[offset..<(offset + length)].allSatisfy { $0 == 0 }
        }), valid & 3 != 0 || b[GeneratedHistorySchema.Record.rhtSource] == GeneratedHistorySchema.RhtSources.none else {
            throw CardHistoryError.invalidRecord
        }
    }

    public func reading(receivedAt: Date) -> SensorReading {
        let b = [UInt8](raw)
        var sensors: [String: SensorBlock] = [:]
        func has(_ bit: Int) -> Bool { valid & (1 << bit) != 0 }
        func signed(_ offset: Int) -> SensorValue { .integer(Int64(Int32(bitPattern: b.historyU32(offset)))) }
        func unsigned(_ offset: Int) -> SensorValue { .integer(Int64(b.historyU32(offset))) }
        func block(_ key: String, id: UInt8, values: [String: SensorValue], available: Bool? = nil) {
            sensors[key] = SensorBlock(id: id, error: (available ?? !values.isEmpty) ? 0 : -61, values: values)
        }
        block("opt4001", id: 1, values: has(4) ? ["millilux": unsigned(GeneratedHistorySchema.Record.millilux)] : [:])
        var bme: [String: SensorValue] = [:]
        if has(5) { bme["temperature_mc"] = signed(GeneratedHistorySchema.Record.bmeTemperatureMc) }
        if has(6) { bme["humidity_mpercent"] = unsigned(GeneratedHistorySchema.Record.bmeHumidityMpercent) }
        if has(2) { bme["pressure_pa"] = unsigned(GeneratedHistorySchema.Record.pressurePa) }
        if has(7) {
            bme["gas_resistance_ohm"] = unsigned(GeneratedHistorySchema.Record.gasResistanceOhm)
        }
        bme["gas_valid"] = .boolean(b[GeneratedHistorySchema.Record.gasValid] == 1)
        bme["heater_stable"] = .boolean(b[GeneratedHistorySchema.Record.heaterStable] == 1)
        block("bme690", id: 2, values: bme, available: has(2) || has(5) || has(6) || has(7))
        var sgp: [String: SensorValue] = [:]
        if has(8) { sgp["raw_voc"] = .integer(Int64(b.historyU16(GeneratedHistorySchema.Record.rawVoc))) }
        if has(9) { sgp["raw_nox"] = .integer(Int64(b.historyU16(GeneratedHistorySchema.Record.rawNox))) }
        if has(10) { sgp["voc_index"] = .integer(Int64(b.historyU16(GeneratedHistorySchema.Record.vocIndex))) }
        if has(11) { sgp["nox_index"] = .integer(Int64(b.historyU16(GeneratedHistorySchema.Record.noxIndex))) }
        if !sgp.isEmpty {
            sgp["nox_valid"] = .boolean(has(9))
            sgp["voc_index_valid"] = .boolean(has(10))
            sgp["nox_index_valid"] = .boolean(has(11))
        }
        block("sgp41", id: 3, values: sgp)
        var stcc: [String: SensorValue] = [:]
        if has(0) { stcc["temperature_mc"] = signed(GeneratedHistorySchema.Record.temperatureMc) }
        if has(1) { stcc["humidity_mpercent"] = unsigned(GeneratedHistorySchema.Record.humidityMpercent) }
        if has(3) {
            stcc["co2_ppm"] = .integer(Int64(Int16(bitPattern: b.historyU16(GeneratedHistorySchema.Record.co2Ppm))))
        }
        stcc["status"] = .integer(Int64(b.historyU16(GeneratedHistorySchema.Record.stccStatus)))
        stcc["self_test_result"] = .integer(Int64(b.historyU16(GeneratedHistorySchema.Record.stccSelfTest)))
        stcc["rht_source"] = .integer(Int64(b[GeneratedHistorySchema.Record.rhtSource]))
        block("stcc4", id: 4, values: stcc, available: has(0) || has(1) || has(3))
        block("sfa40", id: 7, values: has(12) ? ["hcho_millippb": signed(GeneratedHistorySchema.Record.hchoMillippb)] : [:])
        let validMask = sensors.values.reduce(UInt16(0)) { $1.error == 0 ? $0 | (1 << ($1.id - 1)) : $0 }
        return SensorReading(sequence: sequence, uptimeMilliseconds: UInt64(uptimeSeconds) * 1_000,
            enabledMask: 0x4f, validMask: validMask, sensors: sensors, receivedAt: receivedAt, rawFrame: raw,
            measuredAt: unixSeconds == 0 ? nil : Date(timeIntervalSince1970: Double(unixSeconds)),
            measurementSource: "card-history-v1")
    }
}

extension [UInt8] {
    func historyU16(_ p: Int) -> UInt16 { UInt16(self[p]) | UInt16(self[p + 1]) << 8 }
    func historyU32(_ p: Int) -> UInt32 { (0..<4).reduce(0) { $0 | UInt32(self[p + $1]) << ($1 * 8) } }
    func historyU64(_ p: Int) -> UInt64 { (0..<8).reduce(0) { $0 | UInt64(self[p + $1]) << ($1 * 8) } }
}
