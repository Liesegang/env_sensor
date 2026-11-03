import Foundation
import SQLite3
import Testing
@testable import EnvSensorCore

struct StorageTests {
    private func temporaryURL() -> URL {
        FileManager.default.temporaryDirectory.appendingPathComponent("env-sensor-tests-" + UUID().uuidString)
            .appendingPathComponent("readings.sqlite")
    }

    @Test func readNotifyDedupAndDeviceSelectionSurviveRelaunch() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        var originalID: Int64 = 0
        do {
            let store = try SensorStore(url: url)
            try store.registerDevice(id: "card", name: "Card Sensor", enabled: true)
            let packets = try capturedPackets()
            let assembler = FragmentAssembler()
            var inserted: [StoredReading] = []
            for (event, bytes, date) in packets {
                let sample = try event == "read" ? EnvSensorProtocol.decode(bytes, receivedAt: date)
                    : #require(try assembler.push(bytes, receivedAt: date))
                if let record = try store.save(sample, deviceID: "card") { inserted.append(record) }
            }
            #expect(inserted.count == 2)
            #expect(try store.counts().total == 2)
            #expect(try store.devices().first?.enabled == true)
            originalID = try #require(inserted.last?.id)
        }
        do {
            let store = try SensorStore(url: url)
            #expect(try store.devices().first?.enabled == true)
            #expect(try store.latestReadings().first?.id == originalID)
            let latest = try #require(store.latestReadings().first?.reading)
            #expect(try store.save(latest, deviceID: "card") == nil)
            try store.registerDevice(id: "card", name: "Renamed")
            #expect(try store.devices().first?.enabled == true)
            try store.setDeviceEnabled(id: "card", enabled: false)
            #expect(try store.devices().first?.enabled == false)
            #expect(try store.counts().samplesByDevice["card"] == 2)
        }
    }

    @Test func bootResetReusesSequenceWithoutOverwritingHistory() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        var previousBoot = ""
        do {
            let store = try SensorStore(url: url)
            let first = try #require(try store.save(fixtureReading(), deviceID: "card"))
            let second = try #require(try store.save(fixtureReading(sequence: 2, uptime: 65_000), deviceID: "card"))
            #expect(first.bootID == second.bootID)
            previousBoot = second.bootID
        }
        let store = try SensorStore(url: url)
        let rebooted = try #require(try store.save(fixtureReading(), deviceID: "card"))
        #expect(rebooted.bootID != previousBoot)
        #expect(try store.save(fixtureReading(), deviceID: "card") == nil)
        #expect(try store.counts().total == 3)
        #expect(try store.history(deviceID: "card").map(\.id) == [3, 2, 1])
        let another = try #require(try store.save(fixtureReading(), deviceID: "outdoor"))
        #expect(another.bootID != rebooted.bootID)
        #expect(try store.latestReadings().count == 2)
    }

    @Test func conflictingSnapshotRollsBackRatherThanReplacingCommittedReading() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let reading = try fixtureReading()
        try store.save(reading, deviceID: "card", deviceName: "Card")
        var conflict = reading.rawFrame
        conflict[30] ^= 1 // Different illuminance, identical sequence and uptime.
        let different = try EnvSensorProtocol.decode(conflict)
        #expect(throws: SensorStoreError.self) { try store.save(different, deviceID: "card", deviceName: "Not committed") }
        #expect(try store.counts().total == 1)
        #expect(try store.devices().first?.name == "Card")
        #expect(try store.latestReadings().first?.reading.rawFrame == reading.rawFrame)
    }

    @Test func exportsAreNonDestructiveFilterableAndBatchCursorIsStable() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let first = try fixtureReading()
        let later = first.receivedAt.addingTimeInterval(60)
        try store.save(first, deviceID: "card", deviceName: "Card, \"office\"\nroom")
        try store.save(fixtureReading(sequence: 2, uptime: 65_000, date: later), deviceID: "card")
        try store.save(first, deviceID: "room", deviceName: "=formula")
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .millisecondsSince1970
        let export = try decoder.decode(ReadingExportBatch.self, from: store.exportJSON())
        #expect(export.schema == "env-sensor-export-v1")
        #expect(export.readings.count == 3)
        #expect(export.readings[0].reading.rawFrame == first.rawFrame)
        let filtered = try decoder.decode(ReadingExportBatch.self, from: store.exportJSON(deviceID: "card", since: later))
        #expect(filtered.readings.map(\.reading.sequence) == [2])
        let batch = try store.exportBatch(limit: 2)
        #expect(batch.readings.map(\.id) == [1, 2])
        #expect(try store.exportBatch(afterID: batch.nextID, limit: 2).readings.map(\.id) == [3])
        #expect(try store.exportBatch(afterID: 3).nextID == 3)
        let csv = try String(decoding: store.exportCSV(), as: UTF8.self)
        #expect(csv.contains("\"Card, \"\"office\"\"\nroom\""))
        #expect(csv.contains("\"'=formula\""))
        #expect(csv.contains("\"sgp41.voc_index_valid\""))
        #expect(try store.counts().total == 3)
    }

    @Test func wideUptimePersistsWithoutSQLiteIntegerOverflow() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        do {
            let store = try SensorStore(url: url)
            try store.save(fixtureReading(sequence: UInt32.max, uptime: UInt64.max), deviceID: "wide")
        }
        let store = try SensorStore(url: url)
        #expect(try store.latestReadings().first?.reading.uptimeMilliseconds == UInt64.max)
        #expect(try store.save(fixtureReading(sequence: UInt32.max, uptime: UInt64.max), deviceID: "wide") == nil)
    }

    @Test func futureDatabaseVersionIsPreservedAndRejected() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        do {
            let store = try SensorStore(url: url)
            try store.save(fixtureReading(), deviceID: "card")
        }
        var database: OpaquePointer?
        #expect(sqlite3_open(url.path, &database) == SQLITE_OK)
        #expect(sqlite3_exec(database, "PRAGMA user_version = 99", nil, nil, nil) == SQLITE_OK)
        sqlite3_close(database)
        #expect(throws: SensorStoreError.self) { try SensorStore(url: url) }
        #expect(sqlite3_open(url.path, &database) == SQLITE_OK)
        var query: OpaquePointer?
        #expect(sqlite3_prepare_v2(database, "SELECT COUNT(*) FROM samples", -1, &query, nil) == SQLITE_OK)
        #expect(sqlite3_step(query) == SQLITE_ROW)
        #expect(sqlite3_column_int(query, 0) == 1)
        sqlite3_finalize(query)
        sqlite3_close(database)
    }
}
