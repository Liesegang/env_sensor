import Foundation
import SQLite3
import Testing
@testable import EnvSensorCore

struct ChartStorageTests {
    private func temporaryURL() -> URL {
        FileManager.default.temporaryDirectory.appendingPathComponent("env-chart-tests-" + UUID().uuidString)
            .appendingPathComponent("readings.sqlite")
    }

    @Test func oneSecondStreamCoversWholeDayWithoutDecodingDiscardedRows() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        try store.registerDevice(id: "outdoor", name: "Outdoor")
        let start = Date(timeIntervalSince1970: 1_700_000_040) // UTC minute boundary.
        try seedOneSecondDay(url: url, start: start)
        let points = try store.chartReadings(deviceID: "outdoor", since: start)
        #expect(points.count == 1_440)
        #expect(points.first?.reading.sequence == 60)
        #expect(points.last?.reading.sequence == 86_400)
        #expect(points.first?.reading.receivedAt == start.addingTimeInterval(59))
        #expect(points.last?.reading.receivedAt == start.addingTimeInterval(86_399))
        #expect(try store.counts().total == 86_400)
        #expect(try store.chartReadings(deviceID: "outdoor", since: start.addingTimeInterval(43_200)).count == 720)
        #expect(try store.chartReadings(deviceID: "different-device", since: start).isEmpty)
    }

    @Test func bucketsPreserveRebootsMissingValuesAndTimeFilter() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let start = Date(timeIntervalSince1970: 1_700_000_040)
        try store.save(fixtureReading(sequence: 1, uptime: 10_000, date: start.addingTimeInterval(-1)), deviceID: "card")
        try store.save(fixtureReading(sequence: 2, uptime: 11_000, date: start), deviceID: "card")
        try store.save(fixtureReading(sequence: 3, uptime: 12_000, date: start.addingTimeInterval(1)), deviceID: "card")
        // Reboot in the same UTC minute must produce a second chart segment.
        try store.save(fixtureReading(sequence: 1, uptime: 1_000, date: start.addingTimeInterval(2)), deviceID: "card")
        try store.save(fixtureReading(sequence: 2, uptime: 60_000, date: start.addingTimeInterval(61)), deviceID: "card")
        let missing = SensorReading(sequence: 3, uptimeMilliseconds: 61_000, enabledMask: 1,
            validMask: 0, sensors: ["opt4001": SensorBlock(id: 1, error: -5, values: [:])],
            receivedAt: start.addingTimeInterval(62), rawFrame: Data([1]))
        try store.save(missing, deviceID: "card")
        try store.save(fixtureReading(date: start), deviceID: "room")
        let points = try store.chartReadings(deviceID: "card", since: start)
        #expect(points.map(\.reading.sequence) == [3, 1, 3])
        #expect(points[0].bootID != points[1].bootID)
        #expect(points[1].bootID == points[2].bootID)
        #expect(points[2].reading.sensors["opt4001"]?.error == -5)
        #expect(points[2].reading.value(sensor: "opt4001", field: "millilux") == nil)
        #expect(throws: SensorStoreError.self) { try store.chartReadings(deviceID: "card", since: start, bucketSeconds: 0) }
    }

    @Test func schemaOneUpgradesWithoutDeletingDeviceOrSample() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        do {
            let store = try SensorStore(url: url)
            try store.save(fixtureReading(), deviceID: "card")
            try store.setDeviceEnabled(id: "card", enabled: true)
        }
        var database: OpaquePointer?
        #expect(sqlite3_open(url.path, &database) == SQLITE_OK)
        #expect(sqlite3_exec(database, "DROP INDEX samples_device_received_at", nil, nil, nil) == SQLITE_OK)
        #expect(sqlite3_exec(database, "DROP INDEX samples_history_identity; DROP INDEX samples_device_measured_at; DROP INDEX samples_device_effective_time; ALTER TABLE samples DROP COLUMN measured_at; ALTER TABLE samples DROP COLUMN source; ALTER TABLE samples DROP COLUMN history_record; DROP TABLE history_cursors; DROP TABLE history_clocks", nil, nil, nil) == SQLITE_OK)
        #expect(sqlite3_exec(database, "PRAGMA user_version = 1", nil, nil, nil) == SQLITE_OK)
        sqlite3_close(database)
        let store = try SensorStore(url: url)
        #expect(try store.counts().total == 1)
        #expect(try store.devices().first?.enabled == true)
        #expect(try store.chartReadings(deviceID: "card", since: .distantPast).count == 1)
        #expect(sqlite3_open(url.path, &database) == SQLITE_OK)
        var statement: OpaquePointer?
        #expect(sqlite3_prepare_v2(database, "PRAGMA user_version", -1, &statement, nil) == SQLITE_OK)
        #expect(sqlite3_step(statement) == SQLITE_ROW)
        #expect(sqlite3_column_int(statement, 0) == 3)
        sqlite3_finalize(statement)
        sqlite3_close(database)
    }

    /// Only each minute's final row contains valid JSON. If chartReadings starts
    /// decoding the unselected 84,960 payloads, this test fails immediately.
    private func seedOneSecondDay(url: URL, start: Date) throws {
        var database: OpaquePointer?
        #expect(sqlite3_open(url.path, &database) == SQLITE_OK)
        defer { sqlite3_close(database) }
        #expect(sqlite3_exec(database, "BEGIN", nil, nil, nil) == SQLITE_OK)
        var statement: OpaquePointer?
        let sql = "INSERT INTO samples(device_id, boot_id, sequence, uptime_ms, received_at, payload_json, raw_frame) VALUES ('outdoor', 'one-day-boot', ?, ?, ?, ?, X'01')"
        #expect(sqlite3_prepare_v2(database, sql, -1, &statement, nil) == SQLITE_OK)
        defer { sqlite3_finalize(statement) }
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .millisecondsSince1970
        let template = try fixtureReading()
        let transient = unsafeBitCast(-1, to: sqlite3_destructor_type.self)
        for second in 0..<86_400 {
            let sequence = UInt32(second + 1)
            let date = start.addingTimeInterval(Double(second))
            let payload: Data
            if second % 60 == 59 {
                payload = try encoder.encode(SensorReading(sequence: sequence,
                    uptimeMilliseconds: UInt64(second * 1_000), enabledMask: template.enabledMask,
                    validMask: template.validMask, sensors: template.sensors, receivedAt: date, rawFrame: Data()))
            } else {
                payload = Data("not decoded by chart query".utf8)
            }
            #expect(sqlite3_bind_int64(statement, 1, Int64(sequence)) == SQLITE_OK)
            #expect(sqlite3_bind_text(statement, 2, String(second * 1_000), -1, transient) == SQLITE_OK)
            #expect(sqlite3_bind_double(statement, 3, date.timeIntervalSince1970) == SQLITE_OK)
            payload.withUnsafeBytes { bytes in
                #expect(sqlite3_bind_blob(statement, 4, bytes.baseAddress, Int32(bytes.count), transient) == SQLITE_OK)
            }
            #expect(sqlite3_step(statement) == SQLITE_DONE)
            sqlite3_reset(statement)
            sqlite3_clear_bindings(statement)
        }
        #expect(sqlite3_exec(database, "COMMIT", nil, nil, nil) == SQLITE_OK)
    }
}
