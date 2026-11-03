import Foundation
import SQLite3
import Testing
@testable import EnvSensorCore

struct BackgroundHistoryTests {
    private func temporaryURL() -> URL {
        FileManager.default.temporaryDirectory.appendingPathComponent("env-background-" + UUID().uuidString)
            .appendingPathComponent("readings.sqlite")
    }

    @Test func oneSecondDayIsAggregatedOnAReadOnlySnapshotWithoutDroppingSavedRows() async throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let start = Date(timeIntervalSince1970: 1700000040)
        try store.save(fixtureReading(date: start), deviceID: "card")
        var database: OpaquePointer?
        #expect(sqlite3_open(url.path, &database) == SQLITE_OK)
        let sql = """
        WITH RECURSIVE seconds(n) AS (VALUES(1) UNION ALL SELECT n+1 FROM seconds WHERE n<86399)
        INSERT INTO samples(device_id,boot_id,sequence,uptime_ms,received_at,payload_json,raw_frame)
        SELECT seed.device_id,seed.boot_id,n+1,CAST(n*1000 AS TEXT),seed.received_at+n,
          CAST(json_set(CAST(seed.payload_json AS TEXT),'$.sequence',n+1,'$.uptimeMilliseconds',n*1000,
                        '$.receivedAt',(seed.received_at+n)*1000) AS BLOB),seed.raw_frame
        FROM seconds CROSS JOIN samples AS seed WHERE seed.id=1
        """
        let inserted = sqlite3_exec(database, sql, nil, nil, nil)
        sqlite3_close(database)
        #expect(inserted == SQLITE_OK)
        let clock = ContinuousClock()
        let began = clock.now
        let snapshot = try await store.loadHistorySnapshot(deviceID: "card", since: start,
            until: start.addingTimeInterval(86399), bucketSeconds: 60)
        let elapsed = clock.now - began
        #expect(snapshot.readings.count == 1440)
        #expect(snapshot.readings.last?.reading.sequence == 86400)
        #expect(snapshot.timeSummary.legacyReceivedCount == 86400)
        #expect(snapshot.availableRange == start...start.addingTimeInterval(86399))
        #expect(try store.counts().total == 86400)
        #expect(elapsed < .seconds(3))
        print("86,400-row background snapshot: \(elapsed)")
        let overview = try await store.loadOverview()
        #expect(overview.recent.count == 200 && overview.counts.total == 86400)
        #expect(overview.latest.first?.reading.sequence == 86400)
        let rangeExport = try await store.loadExportJSON(deviceID: "card", since: start, until: start.addingTimeInterval(59))
        let decoder = JSONDecoder(); decoder.dateDecodingStrategy = .millisecondsSince1970
        #expect(try decoder.decode(ReadingExportBatch.self, from: rangeExport).readings.count == 60)
    }

    @Test func heldReadSnapshotAllowsBLECommitAndKeepsItsOwnConsistentCounts() async throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        try store.save(fixtureReading(), deviceID: "card")
        let entered = DispatchSemaphore(value: 0)
        let release = DispatchSemaphore(value: 0)
        defer { release.signal() }
        let reader = Task {
            try await store.readInBackground { connection in
                let first = try connection.counts().total
                entered.signal()
                _ = release.wait(timeout: .now() + 3)
                return (first, try connection.counts().total)
            }
        }
        let started = await awaitSignal(entered)
        #expect(started)
        let clock = ContinuousClock(); let began = clock.now
        try store.save(fixtureReading(sequence: 2, uptime: 65000), deviceID: "card")
        let writeTime = clock.now - began
        release.signal()
        let counts = try await reader.value
        #expect(counts.0 == 1 && counts.1 == 1)
        #expect(try store.counts().total == 2)
        #expect(writeTime < .milliseconds(500))
        print("BLE commit with read snapshot open: \(writeTime)")
    }

    @Test func cancelInterruptsReadAndLeavesTheDurableWriterUsable() async throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        try store.save(fixtureReading(), deviceID: "card")
        let entered = DispatchSemaphore(value: 0)
        let release = DispatchSemaphore(value: 0)
        defer { release.signal() }
        let reader = Task {
            try await store.readInBackground { connection in
                _ = try connection.counts()
                entered.signal()
                _ = release.wait(timeout: .now() + 3)
                return try connection.chartReadings(deviceID: "card", since: .distantPast)
            }
        }
        let started = await awaitSignal(entered)
        #expect(started)
        reader.cancel(); release.signal()
        await #expect(throws: CancellationError.self) { try await reader.value }
        try store.save(fixtureReading(sequence: 2, uptime: 65000), deviceID: "card")
        #expect(try store.counts().total == 2)
        #expect(try await store.loadOverview().counts.total == 2)
    }

    @MainActor @Test func oldBacklogAndClockCorrectionPublishHistoryRevisionEvenWhenRecentRowsStayIdentical() async throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let start = Date(timeIntervalSince1970: 1700000040)
        for index in 1...201 {
            try store.save(fixtureReading(sequence: UInt32(index), uptime: UInt64(index * 1000),
                date: start.addingTimeInterval(Double(index))), deviceID: "card")
        }
        let model = SensorAppModel(store: store, isDemo: true)
        try await waitUntil { model.historyRevision > 0 }
        let recent = model.recent
        let firstRevision = model.historyRevision
        var record = Data(repeating: 0, count: 68); record[0] = 1
        var page = Data([0x48,0x50,1,0])
        page.historyAppend(UInt32(1)); page.historyAppend(UInt64(1)); page.historyAppend(UInt32(1))
        page.historyAppend(UInt16(1)); page.historyAppend(UInt16(68)); page.historyAppend(UInt32(0)); page.historyAppend(UInt32(0))
        page.append(record)
        let crc = CardImageProtocol.crc32(page)
        for index in 0..<4 { page[24 + index] = UInt8(truncatingIfNeeded: crc >> (index * 8)) }
        try store.saveHistoryPage(CardHistoryPage(packet: page), deviceID: "card", expectedCursor: .beginning)
        model.refresh()
        try await waitUntil { model.historyRevision > firstRevision }
        #expect(model.recent == recent && model.counts?.total == 202)
        let secondRevision = model.historyRevision
        try store.applyDeviceClock(.init(bootID: 1, revision: 1, unixAnchorMilliseconds: 1577836800000,
            uptimeAnchorMilliseconds: 0), deviceID: "card")
        model.refresh()
        try await waitUntil { model.historyRevision > secondRevision }
        #expect(model.recent == recent)
        #expect(try await model.loadHistorySnapshot(deviceID: "card", since: .distantPast, until: .distantFuture,
            bucketSeconds: 60).timeSummary.unknownTimeCount == 0)
    }

    @MainActor private func waitUntil(_ predicate: @MainActor () -> Bool) async throws {
        for _ in 0..<300 {
            if predicate() { return }
            try await Task.sleep(for: .milliseconds(10))
        }
        Issue.record("Background snapshot was not published within 3 seconds")
    }

    private func awaitSignal(_ semaphore: DispatchSemaphore) async -> Bool {
        await withCheckedContinuation { continuation in
            DispatchQueue.global().async {
                continuation.resume(returning: semaphore.wait(timeout: .now() + 2) == .success)
            }
        }
    }
}
