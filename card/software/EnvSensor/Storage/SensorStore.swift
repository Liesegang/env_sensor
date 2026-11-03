import Foundation
import SQLite3

public struct SensorStoreError: Error, Sendable, LocalizedError {
    public let message: String
    public var errorDescription: String? { message }
}

/// Synchronous durable saves keep a BLE delegate's wake time short. All SQLite
/// access is serialized by the lock; UI and export callers may use other queues.
public final class SensorStore: @unchecked Sendable {
    public let url: URL
    private let lock = NSLock()
    private var database: OpaquePointer?
    private let encoder: JSONEncoder
    private let decoder: JSONDecoder
    private var queryCancellation: HistoryQueryCancellation?
    private static let chartTime = "COALESCE(measured_at, CASE WHEN source = 'legacy' THEN received_at END)"

    public static func defaultURL() throws -> URL {
        let support = try FileManager.default.url(for: .applicationSupportDirectory,
            in: .userDomainMask, appropriateFor: nil, create: true)
        return support.appendingPathComponent("EnvSensor", isDirectory: true)
            .appendingPathComponent("readings.sqlite")
    }

    public convenience init(path: String) throws { try self.init(url: URL(fileURLWithPath: path)) }

    public init(url: URL) throws {
        self.url = url
        encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        encoder.dateEncodingStrategy = .millisecondsSince1970
        decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .millisecondsSince1970
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try protectFiles()
        let result = sqlite3_open_v2(url.path, &database,
            SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, nil)
        guard result == SQLITE_OK, database != nil else {
            let message = database.map { String(cString: sqlite3_errmsg($0)) } ?? "SQLite を開けません。"
            if database != nil { sqlite3_close(database); database = nil }
            throw SensorStoreError(message: message)
        }
        do {
            sqlite3_busy_timeout(database, 5_000)
            try execute("PRAGMA foreign_keys = ON")
            try execute("PRAGMA journal_mode = WAL")
            // FULL keeps committed readings through process termination/power failure.
            try execute("PRAGMA synchronous = FULL")
            try migrate()
            try protectFiles()
        } catch {
            sqlite3_close(database)
            database = nil
            throw error
        }
    }

    deinit { if database != nil { sqlite3_close(database) } }

    /// Its own read-only WAL connection and lock allow BLE commits to continue.
    /// The reader does not migrate the schema or change file attributes.
    private init(readOnlyURL: URL) throws {
        url = readOnlyURL
        encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        encoder.dateEncodingStrategy = .millisecondsSince1970
        decoder = JSONDecoder(); decoder.dateDecodingStrategy = .millisecondsSince1970
        let result = sqlite3_open_v2(url.path, &database, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nil)
        guard result == SQLITE_OK, database != nil else {
            let message = database.map { String(cString: sqlite3_errmsg($0)) } ?? "履歴を開けません。"
            if database != nil { sqlite3_close(database); database = nil }
            throw SensorStoreError(message: message)
        }
        sqlite3_busy_timeout(database, 2_000)
    }

    /// One coherent snapshot without the writer's Swift lock. Cancellation
    /// interrupts SQL and is checked between decoded rows as well.
    func readInBackground<Value: Sendable>(_ operation: @escaping @Sendable (SensorStore) throws -> Value) async throws -> Value {
        let cancellation = HistoryQueryCancellation()
        let readingURL = url
        return try await withTaskCancellationHandler {
            try await Task.detached(priority: .userInitiated) {
                try cancellation.check()
                let reader = try SensorStore(readOnlyURL: readingURL)
                reader.queryCancellation = cancellation
                sqlite3_progress_handler(reader.database, 1_000, { context in
                    guard let context else { return 0 }
                    return Unmanaged<HistoryQueryCancellation>.fromOpaque(context).takeUnretainedValue().isCancelled ? 1 : 0
                }, Unmanaged.passUnretained(cancellation).toOpaque())
                defer { sqlite3_progress_handler(reader.database, 0, nil, nil) }
                do {
                    try reader.execute("BEGIN")
                    let value = try operation(reader)
                    try cancellation.check()
                    try reader.execute("COMMIT")
                    return value
                } catch {
                    try? reader.execute("ROLLBACK")
                    if cancellation.isCancelled { throw CancellationError() }
                    throw error
                }
            }.value
        } onCancel: { cancellation.cancel() }
    }

    public func loadHistorySnapshot(deviceID: String, since: Date, until: Date, bucketSeconds: Int) async throws -> HistoryDataSnapshot {
        try await readInBackground { reader in
            HistoryDataSnapshot(readings: try reader.chartReadings(deviceID: deviceID, since: since, until: until, bucketSeconds: bucketSeconds),
                availableRange: try reader.measurementRange(deviceID: deviceID),
                timeSummary: try reader.timeSummary(deviceID: deviceID, since: since, until: until))
        }
    }

    public func loadMeasurementRange(deviceID: String) async throws -> ClosedRange<Date>? {
        try await readInBackground { try $0.measurementRange(deviceID: deviceID) }
    }

    public func loadOverview() async throws -> SensorOverviewSnapshot {
        try await readInBackground { reader in
            SensorOverviewSnapshot(devices: try reader.devices(), latest: try reader.latestReadings(),
                recent: try reader.history(limit: 200), counts: try reader.counts())
        }
    }

    public func loadExportJSON(deviceID: String? = nil, since: Date? = nil, until: Date? = nil) async throws -> Data {
        try await readInBackground { try $0.exportJSON(deviceID: deviceID, since: since, until: until) }
    }

    public func loadExportCSV(deviceID: String? = nil, since: Date? = nil, until: Date? = nil) async throws -> Data {
        try await readInBackground { try $0.exportCSV(deviceID: deviceID, since: since, until: until) }
    }

    @discardableResult
    public func registerDevice(id: String, name: String, enabled: Bool? = nil,
                               seenAt: Date = Date()) throws -> DeviceRecord {
        try synchronized {
            try upsertDevice(id: id, name: name, enabled: enabled, seenAt: seenAt)
            guard let result = try loadDevices().first(where: { $0.id == id }) else {
                throw SensorStoreError(message: "機器の登録に失敗しました。")
            }
            return result
        }
    }

    public func setDeviceEnabled(id: String, enabled: Bool) throws {
        try synchronized {
            let statement = try prepare("UPDATE devices SET enabled = ? WHERE id = ?")
            try statement.bind(Int64(enabled ? 1 : 0), at: 1)
            try statement.bind(id, at: 2)
            try statement.run()
            guard sqlite3_changes(database) == 1 else { throw SensorStoreError(message: "機器が登録されていません。") }
        }
    }

    public func devices() throws -> [DeviceRecord] { try synchronized { try loadDevices() } }

    /// Read and Notify of one snapshot produce one row. A decreasing uptime
    /// starts a new boot session, including after an app restart. OE v1 has no
    /// boot UUID; this assumes the live stream is delivered in chronological order.
    @discardableResult
    public func save(_ reading: SensorReading, deviceID: String,
                     deviceName: String? = nil) throws -> StoredReading? {
        try synchronized {
            try transaction {
                let stateQuery = try prepare("SELECT name, boot_id, last_uptime FROM devices WHERE id = ?")
                try stateQuery.bind(deviceID, at: 1)
                let exists = try stateQuery.step()
                let name = deviceName ?? (exists ? stateQuery.string(0) : "環境センサー")
                var bootID = exists ? stateQuery.string(1) : UUID().uuidString
                let lastUptime = exists ? UInt64(stateQuery.string(2)) : nil
                if let lastUptime, reading.uptimeMilliseconds < lastUptime {
                    bootID = UUID().uuidString
                }
                try upsertDevice(id: deviceID, name: name, enabled: nil, seenAt: reading.receivedAt)
                let duplicate = try prepare("SELECT raw_frame FROM samples WHERE device_id = ? AND boot_id = ? AND sequence = ? AND uptime_ms = ?")
                try duplicate.bind(deviceID, at: 1)
                try duplicate.bind(bootID, at: 2)
                try duplicate.bind(Int64(reading.sequence), at: 3)
                try duplicate.bind(String(reading.uptimeMilliseconds), at: 4)
                if try duplicate.step() {
                    guard duplicate.data(0) == reading.rawFrame else {
                        throw SensorStoreError(message: "同じ測定の連番と稼働時間に、異なるデータが届きました。")
                    }
                    return nil
                }
                let insert = try prepare("INSERT INTO samples(device_id, boot_id, sequence, uptime_ms, received_at, payload_json, raw_frame) VALUES (?, ?, ?, ?, ?, ?, ?)")
                try insert.bind(deviceID, at: 1)
                try insert.bind(bootID, at: 2)
                try insert.bind(Int64(reading.sequence), at: 3)
                try insert.bind(String(reading.uptimeMilliseconds), at: 4)
                try insert.bind(reading.receivedAt.timeIntervalSince1970, at: 5)
                try insert.bind(encoder.encode(reading), at: 6)
                try insert.bind(reading.rawFrame, at: 7)
                try insert.run()
                let rowID = sqlite3_last_insert_rowid(database)
                let update = try prepare("UPDATE devices SET boot_id = ?, last_uptime = ?, last_sequence = ? WHERE id = ?")
                try update.bind(bootID, at: 1)
                try update.bind(String(reading.uptimeMilliseconds), at: 2)
                try update.bind(Int64(reading.sequence), at: 3)
                try update.bind(deviceID, at: 4)
                try update.run()
                return StoredReading(id: rowID, deviceID: deviceID, bootID: bootID, reading: reading)
            }
        }
    }

    public func latestReadings() throws -> [StoredReading] {
        try synchronized {
            try loadReadings(sql: "SELECT id, device_id, boot_id, payload_json, measured_at FROM samples WHERE id IN (SELECT (SELECT id FROM samples WHERE device_id=devices.id ORDER BY \(Self.chartTime) DESC,id DESC LIMIT 1) FROM devices) ORDER BY \(Self.chartTime) DESC,id DESC")
        }
    }

    public func history(deviceID: String? = nil, limit: Int = 200) throws -> [StoredReading] {
        try synchronized {
            guard limit > 0 else { return [] }
            return try loadReadings(sql: "SELECT id, device_id, boot_id, payload_json, measured_at FROM samples " +
                (deviceID != nil ? "WHERE device_id = ? " : "") + "ORDER BY \(Self.chartTime) DESC, id DESC LIMIT ?",
                strings: deviceID.map { [$0] } ?? [], integers: [Int64(min(limit, 10_000))])
        }
    }

    /// Select the last sample in each time bucket before decoding JSON. This
    /// bounds chart memory even for outdoor's one-second stream. Boot sessions
    /// remain separate so a reboot cannot be hidden by aggregation.
    public func chartReadings(deviceID: String, since: Date, until: Date? = nil, bucketSeconds: Int = 60) throws -> [StoredReading] {
        try synchronized {
            guard since.timeIntervalSince1970.isFinite, bucketSeconds > 0 else {
                throw SensorStoreError(message: "グラフの時間範囲が不正です。")
            }
            let untilFilter = until != nil ? " AND \(Self.chartTime) <= ?" : ""
            let query = try prepare("""
                SELECT s.id, s.device_id, s.boot_id, s.payload_json, s.measured_at FROM samples AS s
                JOIN (
                    SELECT id AS sample_id FROM (
                        SELECT id, ROW_NUMBER() OVER (PARTITION BY CAST(\(Self.chartTime) / ? AS INTEGER), boot_id
                            ORDER BY \(Self.chartTime) DESC, sequence DESC, id DESC) AS rank FROM samples
                        WHERE device_id = ? AND \(Self.chartTime) >= ? \(untilFilter)
                    ) WHERE rank = 1 ORDER BY sample_id DESC LIMIT 10000
                ) AS buckets ON s.id = buckets.sample_id
                ORDER BY COALESCE(s.measured_at, CASE WHEN s.source = 'legacy' THEN s.received_at END), s.id
                """)
            try query.bind(Int64(bucketSeconds), at: 1)
            try query.bind(deviceID, at: 2)
            try query.bind(since.timeIntervalSince1970, at: 3)
            if let until { try query.bind(until.timeIntervalSince1970, at: 4) }
            var result: [StoredReading] = []
            while try query.step() {
                try queryCancellation?.check()
                result.append(StoredReading(id: query.integer(0), deviceID: query.string(1),
                    bootID: query.string(2), reading: try decodeReading(query)))
            }
            return result
        }
    }

    public func measurementRange(deviceID: String) throws -> ClosedRange<Date>? {
        try synchronized {
            let query = try prepare("SELECT MIN(\(Self.chartTime)),MAX(\(Self.chartTime)) FROM samples WHERE device_id=?")
            try query.bind(deviceID, at: 1)
            guard try query.step(), !query.isNull(0), !query.isNull(1) else { return nil }
            return Date(timeIntervalSince1970: query.double(0))...Date(timeIntervalSince1970: query.double(1))
        }
    }

    public func counts() throws -> StoreCounts {
        try synchronized {
            let query = try prepare("SELECT device_id, COUNT(*) FROM samples GROUP BY device_id")
            var counts: [String: Int] = [:]
            while try query.step() { counts[query.string(0)] = Int(query.integer(1)) }
            return StoreCounts(deviceCount: try loadDevices().count,
                               sampleCount: counts.values.reduce(0, +), samplesByDevice: counts)
        }
    }

    /// Stable ascending row IDs let a future uploader/exporter process bounded
    /// batches without modifying or deleting local history.
    public func exportBatch(afterID: Int64 = 0, limit: Int = 500) throws -> ReadingExportBatch {
        try synchronized {
            let samples = try loadReadings(sql: "SELECT id, device_id, boot_id, payload_json, measured_at FROM samples WHERE id > ? ORDER BY id LIMIT ?",
                integers: [afterID, Int64(max(1, min(limit, 10_000)))])
            return ReadingExportBatch(schema: "env-sensor-export-v1", generatedAt: Date(),
                devices: try loadDevices(), readings: samples, nextID: samples.last?.id ?? afterID)
        }
    }

    public func exportJSON(deviceID: String? = nil, since: Date? = nil, until: Date? = nil) throws -> Data {
        try synchronized {
            let samples = try exportReadings(deviceID: deviceID, since: since, until: until)
            let batch = ReadingExportBatch(schema: "env-sensor-export-v1", generatedAt: Date(),
                devices: try loadDevices(), readings: samples, nextID: samples.last?.id ?? 0)
            return try encoder.encode(batch)
        }
    }

    public func exportCSV(deviceID: String? = nil, since: Date? = nil, until: Date? = nil) throws -> Data {
        try synchronized {
            let samples = try exportReadings(deviceID: deviceID, since: since, until: until)
            let names = Dictionary(uniqueKeysWithValues: try loadDevices().map { ($0.id, $0.name) })
            let fixed = ["row_id", "device_id", "device_name", "boot_id", "sequence", "uptime_ms", "received_at_utc", "enabled_mask", "valid_mask", "measured_at_utc", "measurement_source"]
            let fields = SensorSchema.sensors.flatMap { sensor in
                [sensor.key + ".error"] + sensor.fields.map { sensor.key + "." + $0.key }
            }
            let formatter = ISO8601DateFormatter()
            formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
            var lines = [(fixed + fields).map(csvCell).joined(separator: ",")]
            for sample in samples {
                let reading = sample.reading
                var cells = [String(sample.id), sample.deviceID, names[sample.deviceID] ?? "", sample.bootID,
                    String(reading.sequence), String(reading.uptimeMilliseconds), formatter.string(from: reading.receivedAt),
                    String(reading.enabledMask), String(reading.validMask), reading.measuredAt.map(formatter.string) ?? "",
                    reading.measurementSource ?? "legacy-received-time"]
                for sensor in SensorSchema.sensors {
                    let block = reading.sensors[sensor.key]
                    cells.append(block.map { String($0.error) } ?? "")
                    cells.append(contentsOf: sensor.fields.map { block?.values[$0.key]?.string ?? "" })
                }
                lines.append(cells.map(csvCell).joined(separator: ","))
            }
            return Data((lines.joined(separator: "\r\n") + "\r\n").utf8)
        }
    }

    private func migrate() throws {
        let query = try prepare("PRAGMA user_version")
        guard try query.step() else { throw SensorStoreError(message: "保存形式を確認できません。") }
        let version = query.integer(0)
        guard version <= 3 else { throw SensorStoreError(message: "このアプリより新しい形式のデータベースです。更新してください。") }
        if version == 0 {
            try transaction {
                try execute("CREATE TABLE IF NOT EXISTS devices (id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL, enabled INTEGER NOT NULL DEFAULT 0 CHECK(enabled IN (0,1)), first_seen REAL NOT NULL, last_seen REAL NOT NULL, boot_id TEXT NOT NULL, last_uptime TEXT, last_sequence INTEGER)")
                try execute("CREATE TABLE IF NOT EXISTS samples (id INTEGER PRIMARY KEY AUTOINCREMENT, device_id TEXT NOT NULL REFERENCES devices(id), boot_id TEXT NOT NULL, sequence INTEGER NOT NULL, uptime_ms TEXT NOT NULL, received_at REAL NOT NULL, payload_json BLOB NOT NULL, raw_frame BLOB NOT NULL, UNIQUE(device_id, boot_id, sequence, uptime_ms))")
                try execute("CREATE INDEX IF NOT EXISTS samples_device_latest ON samples(device_id, id DESC)")
                try execute("CREATE INDEX IF NOT EXISTS samples_received_at ON samples(received_at)")
                try execute("PRAGMA user_version = 1")
            }
        }
        if version < 2 {
            try transaction {
                try execute("CREATE INDEX IF NOT EXISTS samples_device_received_at ON samples(device_id, received_at)")
                try execute("PRAGMA user_version = 2")
            }
        }
        if version < 3 {
            try transaction {
                try execute("ALTER TABLE samples ADD COLUMN measured_at REAL")
                try execute("ALTER TABLE samples ADD COLUMN source TEXT NOT NULL DEFAULT 'legacy'")
                try execute("ALTER TABLE samples ADD COLUMN history_record BLOB")
                try execute("CREATE UNIQUE INDEX samples_history_identity ON samples(device_id, boot_id, sequence) WHERE source = 'card-history-v1'")
                try execute("CREATE INDEX samples_device_measured_at ON samples(device_id, measured_at)")
                try execute("CREATE INDEX samples_device_effective_time ON samples(device_id, \(Self.chartTime) DESC, id DESC)")
                try execute("CREATE TABLE history_cursors(device_id TEXT PRIMARY KEY REFERENCES devices(id), boot_id TEXT NOT NULL, sequence INTEGER NOT NULL)")
                try execute("CREATE TABLE history_clocks(device_id TEXT NOT NULL REFERENCES devices(id), boot_id TEXT NOT NULL, revision INTEGER NOT NULL, utc_anchor TEXT NOT NULL, uptime_anchor INTEGER NOT NULL, PRIMARY KEY(device_id, boot_id))")
                try execute("PRAGMA user_version = 3")
            }
        }
    }

    private func protectFiles() throws {
        #if os(iOS)
        for path in [url.deletingLastPathComponent().path, url.path, url.path + "-wal", url.path + "-shm"]
            where FileManager.default.fileExists(atPath: path) {
            try FileManager.default.setAttributes([.protectionKey: FileProtectionType.completeUntilFirstUserAuthentication], ofItemAtPath: path)
        }
        #endif
    }

    private func upsertDevice(id: String, name: String, enabled: Bool?, seenAt: Date) throws {
        guard !id.isEmpty else { throw SensorStoreError(message: "機器の ID が空です。") }
        let statement = try prepare("INSERT INTO devices(id, name, enabled, first_seen, last_seen, boot_id) VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET name = excluded.name, last_seen = MAX(devices.last_seen, excluded.last_seen), enabled = COALESCE(?, devices.enabled)")
        try statement.bind(id, at: 1)
        try statement.bind(name, at: 2)
        try statement.bind(Int64(enabled == true ? 1 : 0), at: 3)
        try statement.bind(seenAt.timeIntervalSince1970, at: 4)
        try statement.bind(seenAt.timeIntervalSince1970, at: 5)
        try statement.bind(UUID().uuidString, at: 6)
        if let enabled { try statement.bind(Int64(enabled ? 1 : 0), at: 7) }
        else { try statement.bindNull(at: 7) }
        try statement.run()
    }

    private func loadDevices() throws -> [DeviceRecord] {
        let statement = try prepare("SELECT id, name, enabled, first_seen, last_seen FROM devices ORDER BY name, id")
        var result: [DeviceRecord] = []
        while try statement.step() {
            result.append(DeviceRecord(id: statement.string(0), name: statement.string(1),
                enabled: statement.integer(2) != 0, firstSeen: Date(timeIntervalSince1970: statement.double(3)),
                lastSeen: Date(timeIntervalSince1970: statement.double(4))))
        }
        return result
    }

    private func exportReadings(deviceID: String?, since: Date?, until: Date?) throws -> [StoredReading] {
        var predicates: [String] = []
        if deviceID != nil { predicates.append("device_id = ?") }
        if since != nil { predicates.append("\(Self.chartTime) >= ?") }
        if until != nil { predicates.append("\(Self.chartTime) <= ?") }
        let filter = predicates.isEmpty ? "" : "WHERE " + predicates.joined(separator: " AND ")
        return try loadReadings(sql: "SELECT id, device_id, boot_id, payload_json, measured_at FROM samples \(filter) ORDER BY id",
            strings: deviceID.map { [$0] } ?? [], doubles: [since, until].compactMap { $0?.timeIntervalSince1970 })
    }

    private func loadReadings(sql: String, strings: [String] = [], integers: [Int64] = [], doubles: [Double] = []) throws -> [StoredReading] {
        let statement = try prepare(sql)
        var binding: Int32 = 1
        for string in strings { try statement.bind(string, at: binding); binding += 1 }
        for integer in integers { try statement.bind(integer, at: binding); binding += 1 }
        for double in doubles { try statement.bind(double, at: binding); binding += 1 }
        var result: [StoredReading] = []
        while try statement.step() {
            try queryCancellation?.check()
            result.append(StoredReading(id: statement.integer(0), deviceID: statement.string(1),
                bootID: statement.string(2), reading: try decodeReading(statement)))
        }
        return result
    }

    private func csvCell(_ value: String) -> String {
        // Quote every cell for commas/newlines/quotes, and prevent spreadsheet
        // formula execution in user-editable names and textual identifiers.
        let text = value.first.map { "=+@".contains($0) } == true ? "'" + value : value
        return "\"" + text.replacingOccurrences(of: "\"", with: "\"\"") + "\""
    }

    private func decodeReading(_ statement: Statement) throws -> SensorReading {
        let value = try decoder.decode(SensorReading.self, from: statement.data(3))
        return value.measurementSource == "card-history-v1"
            ? value.withMeasuredAt(statement.isNull(4) ? nil : Date(timeIntervalSince1970: statement.double(4))) : value
    }

    public func timeSummary(deviceID: String, since: Date, until: Date) throws -> ReadingTimeSummary {
        try synchronized {
            let query = try prepare("SELECT SUM(CASE WHEN measured_at BETWEEN ? AND ? THEN 1 ELSE 0 END), SUM(CASE WHEN source='legacy' AND received_at BETWEEN ? AND ? THEN 1 ELSE 0 END), SUM(CASE WHEN source='card-history-v1' AND measured_at IS NULL THEN 1 ELSE 0 END) FROM samples WHERE device_id=?")
            try query.bind(since.timeIntervalSince1970, at: 1); try query.bind(until.timeIntervalSince1970, at: 2)
            try query.bind(since.timeIntervalSince1970, at: 3); try query.bind(until.timeIntervalSince1970, at: 4)
            try query.bind(deviceID, at: 5); _ = try query.step()
            return ReadingTimeSummary(measuredCount: Int(query.integer(0)), legacyReceivedCount: Int(query.integer(1)), unknownTimeCount: Int(query.integer(2)))
        }
    }

    public func historyCursor(deviceID: String) throws -> CardHistoryCursor {
        try synchronized { try loadHistoryCursor(deviceID) }
    }

    private func loadHistoryCursor(_ id: String) throws -> CardHistoryCursor {
        let query = try prepare("SELECT boot_id, sequence FROM history_cursors WHERE device_id = ?")
        try query.bind(id, at: 1)
        guard try query.step() else { return .beginning }
        guard let boot = UInt64(query.string(0), radix: 16), let seq = UInt32(exactly: query.integer(1)) else {
            throw CardHistoryError.invalidCursor
        }
        return .init(bootID: boot, sequence: seq)
    }

    /// Page measurements and resume position are one durable commit. Returning
    /// from this method is the only point where the caller may ACK the page.
    @discardableResult
    public func saveHistoryPage(_ page: CardHistoryPage, deviceID: String, expectedCursor: CardHistoryCursor,
                                receivedAt: Date = .now, completesGapRescan: Bool = false) throws -> Int {
        guard !completesGapRescan || page.flags & GeneratedHistorySchema.Flags.more == 0 else {
            throw CardHistoryError.invalidCursor
        }
        return try synchronized {
            try transaction {
                let device = try loadDevices().first { $0.id == deviceID }
                try upsertDevice(id: deviceID, name: device?.name ?? "Card Sensor", enabled: nil, seenAt: receivedAt)
                let storedCursor = try loadHistoryCursor(deviceID)
                var inserted = 0
                for record in page.records {
                    let reading = record.reading(receivedAt: receivedAt)
                    let duplicate = try prepare("SELECT history_record FROM samples WHERE device_id = ? AND boot_id = ? AND sequence = ? AND source = 'card-history-v1'")
                    try duplicate.bind(deviceID, at: 1); try duplicate.bind(page.cursor.bootKey, at: 2)
                    try duplicate.bind(Int64(record.sequence), at: 3)
                    var identity = record.raw
                    identity.replaceSubrange(8..<16, with: Array(repeating: 0, count: 8))
                    if try duplicate.step() {
                        var previous = duplicate.data(0)
                        previous.replaceSubrange(8..<16, with: Array(repeating: 0, count: 8))
                        guard previous == identity else { throw SensorStoreError(message: "同じboot/連番の測定内容が異なります。履歴をACKしません。") }
                        if let date = reading.measuredAt {
                            let update = try prepare("UPDATE samples SET measured_at = ? WHERE device_id = ? AND boot_id = ? AND sequence = ? AND source = 'card-history-v1'")
                            try update.bind(date.timeIntervalSince1970, at: 1); try update.bind(deviceID, at: 2)
                            try update.bind(page.cursor.bootKey, at: 3); try update.bind(Int64(record.sequence), at: 4)
                            try update.run()
                        }
                    } else {
                        let insert = try prepare("INSERT INTO samples(device_id,boot_id,sequence,uptime_ms,received_at,payload_json,raw_frame,measured_at,source,history_record) VALUES (?,?,?,?,?,?,?,?, 'card-history-v1',?)")
                        try insert.bind(deviceID, at: 1); try insert.bind(page.cursor.bootKey, at: 2)
                        try insert.bind(Int64(record.sequence), at: 3); try insert.bind(String(reading.uptimeMilliseconds), at: 4)
                        try insert.bind(receivedAt.timeIntervalSince1970, at: 5); try insert.bind(encoder.encode(reading), at: 6)
                        try insert.bind(record.raw, at: 7)
                        if let date = reading.measuredAt { try insert.bind(date.timeIntervalSince1970, at: 8) }
                        else { try insert.bindNull(at: 8) }
                        try insert.bind(record.raw, at: 9); try insert.run(); inserted += 1
                    }
                }
                if let first = page.records.first {
                    try applyStoredHistoryClock(deviceID: deviceID, bootKey: page.cursor.bootKey,
                                                sequenceRange: first.sequence...page.cursor.sequence)
                }
                if storedCursor == expectedCursor {
                    // An old duplicate response must not move the durable cursor
                    // backwards after a newer request already committed.
                    if completesGapRescan || storedCursor.bootID != page.bootID || page.cursor.sequence >= storedCursor.sequence {
                        let cursor = try prepare("INSERT INTO history_cursors(device_id,boot_id,sequence) VALUES (?,?,?) ON CONFLICT(device_id) DO UPDATE SET boot_id=excluded.boot_id,sequence=excluded.sequence")
                        try cursor.bind(deviceID, at: 1); try cursor.bind(String(page.bootID, radix: 16), at: 2)
                        try cursor.bind(Int64(page.cursor.sequence), at: 3); try cursor.run()
                    }
                }
                return inserted
            }
        }
    }

    /// The firmware explicitly reported COMPLETE+GAP with no retained records.
    /// This is a recovery checkpoint, not an ordinary delayed page response.
    public func finishEmptyHistoryRescan(deviceID: String, expectedCursor: CardHistoryCursor) throws {
        try finishHistoryRescan(deviceID: deviceID, expectedCursor: expectedCursor, finalCursor: .beginning)
    }

    public func finishHistoryRescan(deviceID: String, expectedCursor: CardHistoryCursor, finalCursor: CardHistoryCursor) throws {
        try synchronized {
            try transaction {
                guard try loadHistoryCursor(deviceID) == expectedCursor else { return }
                if finalCursor == .beginning {
                    let remove = try prepare("DELETE FROM history_cursors WHERE device_id=?")
                    try remove.bind(deviceID, at: 1); try remove.run()
                } else {
                    let record = try prepare("SELECT id FROM samples WHERE device_id=? AND boot_id=? AND sequence=? AND source='card-history-v1'")
                    try record.bind(deviceID, at: 1); try record.bind(finalCursor.bootKey, at: 2)
                    try record.bind(Int64(finalCursor.sequence), at: 3)
                    guard try record.step() else { throw CardHistoryError.invalidCursor }
                    let cursor = try prepare("INSERT INTO history_cursors(device_id,boot_id,sequence) VALUES (?,?,?) ON CONFLICT(device_id) DO UPDATE SET boot_id=excluded.boot_id,sequence=excluded.sequence")
                    try cursor.bind(deviceID, at: 1); try cursor.bind(String(finalCursor.bootID, radix: 16), at: 2)
                    try cursor.bind(Int64(finalCursor.sequence), at: 3); try cursor.run()
                }
            }
        }
    }

    public func applyHistoryClock(_ status: CardHistoryStatus, deviceID: String) throws {
        try applyDeviceClock(status.deviceClock, deviceID: deviceID)
    }

    public func applyDeviceClock(_ clock: CardDeviceClock, deviceID: String) throws {
        guard clock.known else { return }
        guard clock.bootID != 0,
              (GeneratedClockSchema.utcMinimumMs...GeneratedClockSchema.utcMaximumMs).contains(clock.unixAnchorMilliseconds) else {
            throw CardHistoryError.invalidHeader
        }
        try synchronized {
            try transaction {
                let device = try loadDevices().first { $0.id == deviceID }
                try upsertDevice(id: deviceID, name: device?.name ?? "Card Sensor", enabled: nil, seenAt: .now)
                let key = CardHistoryCursor(bootID: clock.bootID, sequence: 0).bootKey
                let query = try prepare("SELECT revision, utc_anchor, uptime_anchor FROM history_clocks WHERE device_id=? AND boot_id=?")
                try query.bind(deviceID, at: 1); try query.bind(key, at: 2)
                let exists = try query.step()
                if exists, query.integer(0) >= Int64(clock.revision) { return }
                let oldOffset = exists ? Double(query.string(1)).map { $0 - query.double(2) } : nil
                let offset = clock.offsetSeconds
                let update = try prepare("INSERT INTO history_clocks(device_id,boot_id,revision,utc_anchor,uptime_anchor) VALUES (?,?,?,?,?) ON CONFLICT(device_id,boot_id) DO UPDATE SET revision=excluded.revision,utc_anchor=excluded.utc_anchor,uptime_anchor=excluded.uptime_anchor")
                try update.bind(deviceID, at: 1); try update.bind(key, at: 2)
                try update.bind(Int64(clock.revision), at: 3); try update.bind(String(Double(clock.unixAnchorMilliseconds) / 1_000), at: 4)
                try update.bind(Double(clock.uptimeAnchorMilliseconds) / 1_000, at: 5); try update.run()
                if oldOffset != offset { try applyStoredHistoryClock(deviceID: deviceID, bootKey: key) }
            }
        }
    }

    private func applyStoredHistoryClock(deviceID: String, bootKey: String, sequenceRange: ClosedRange<UInt32>? = nil) throws {
        let query = try prepare("SELECT utc_anchor,uptime_anchor FROM history_clocks WHERE device_id=? AND boot_id=?")
        try query.bind(deviceID, at: 1); try query.bind(bootKey, at: 2)
        guard try query.step(), let utc = Double(query.string(0)) else { return }
        let derived = "(? + CAST(uptime_ms AS REAL)/1000)"
        let update = try prepare("UPDATE samples SET measured_at=CASE WHEN \(derived) BETWEEN \(GeneratedHistorySchema.utcMinimum) AND \(GeneratedHistorySchema.utcMaximum) THEN \(derived) END WHERE device_id=? AND boot_id=? AND source='card-history-v1' AND (measured_at IS NULL OR measured_at != \(derived))" + (sequenceRange != nil ? " AND sequence >= ? AND sequence <= ?" : ""))
        let offset = utc - query.double(1)
        try update.bind(offset, at: 1); try update.bind(offset, at: 2)
        try update.bind(deviceID, at: 3); try update.bind(bootKey, at: 4); try update.bind(offset, at: 5)
        if let sequenceRange {
            try update.bind(Int64(sequenceRange.lowerBound), at: 6); try update.bind(Int64(sequenceRange.upperBound), at: 7)
        }
        try update.run()
    }

    private func synchronized<T>(_ operation: () throws -> T) rethrows -> T {
        lock.lock()
        defer { lock.unlock() }
        return try operation()
    }

    private func transaction<T>(_ operation: () throws -> T) throws -> T {
        try execute("BEGIN IMMEDIATE")
        do {
            let result = try operation()
            try execute("COMMIT")
            return result
        } catch {
            try? execute("ROLLBACK")
            throw error
        }
    }

    private func prepare(_ sql: String) throws -> Statement {
        guard let database else { throw SensorStoreError(message: "データベースが閉じています。") }
        return try Statement(database: database, sql: sql)
    }

    private func execute(_ sql: String) throws {
        guard sqlite3_exec(database, sql, nil, nil, nil) == SQLITE_OK else {
            throw SensorStoreError(message: database.map { String(cString: sqlite3_errmsg($0)) } ?? "SQLite エラー")
        }
    }
}

private final class Statement {
    private let database: OpaquePointer
    private var statement: OpaquePointer?
    private let transient = unsafeBitCast(-1, to: sqlite3_destructor_type.self)

    init(database: OpaquePointer, sql: String) throws {
        self.database = database
        guard sqlite3_prepare_v2(database, sql, -1, &statement, nil) == SQLITE_OK else {
            throw SensorStoreError(message: String(cString: sqlite3_errmsg(database)))
        }
    }
    deinit { sqlite3_finalize(statement) }
    func bind(_ value: String, at index: Int32) throws {
        try check(sqlite3_bind_text(statement, index, value, -1, transient))
    }
    func bind(_ value: Int64, at index: Int32) throws { try check(sqlite3_bind_int64(statement, index, value)) }
    func bind(_ value: Double, at index: Int32) throws { try check(sqlite3_bind_double(statement, index, value)) }
    func bind(_ value: Data, at index: Int32) throws {
        try value.withUnsafeBytes { bytes in
            try check(sqlite3_bind_blob(statement, index, bytes.baseAddress, Int32(bytes.count), transient))
        }
    }
    func bindNull(at index: Int32) throws { try check(sqlite3_bind_null(statement, index)) }
    func run() throws {
        guard try step() == false else { throw SensorStoreError(message: "SQLite が予期しない行を返しました。") }
    }
    func step() throws -> Bool {
        let result = sqlite3_step(statement)
        if result == SQLITE_ROW { return true }
        if result == SQLITE_DONE { return false }
        throw SensorStoreError(message: String(cString: sqlite3_errmsg(database)))
    }
    func string(_ column: Int32) -> String {
        sqlite3_column_text(statement, column).map { String(cString: $0) } ?? ""
    }
    func integer(_ column: Int32) -> Int64 { sqlite3_column_int64(statement, column) }
    func double(_ column: Int32) -> Double { sqlite3_column_double(statement, column) }
    func isNull(_ column: Int32) -> Bool { sqlite3_column_type(statement, column) == SQLITE_NULL }
    func data(_ column: Int32) -> Data {
        let count = Int(sqlite3_column_bytes(statement, column))
        guard count > 0, let bytes = sqlite3_column_blob(statement, column) else { return Data() }
        return Data(bytes: bytes, count: count)
    }
    private func check(_ result: Int32) throws {
        guard result == SQLITE_OK else { throw SensorStoreError(message: String(cString: sqlite3_errmsg(database))) }
    }
}
