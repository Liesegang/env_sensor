import Foundation
import SQLite3
import Testing
@testable import EnvSensorCore

private func historyGolden(_ key: String) throws -> Data {
    var root = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
    for _ in 0..<3 { root.deleteLastPathComponent() }
    let data = try Data(contentsOf: root.appendingPathComponent("shared/protocol/tests/env-history-v1-golden.json"))
    let json = try #require(JSONSerialization.jsonObject(with: data) as? [String: Any])
    let vectors = try #require(json["vectors"] as? [String: String])
    let hex = Array(try #require(vectors[key]))
    return Data(try stride(from: 0, to: hex.count, by: 2).map {
        try #require(UInt8(String(hex[$0...($0 + 1)]), radix: 16))
    })
}

private func historyPut<T: FixedWidthInteger>(_ value: T, at offset: Int, in data: inout Data) {
    for index in 0..<MemoryLayout<T>.size { data[offset + index] = UInt8(truncatingIfNeeded: value >> (index * 8)) }
}

private func historyPage(boot: UInt64 = 0x9abcdef012345678, first: UInt32 = 1001,
                         unix: UInt64? = nil, flags: UInt8 = 1, changeMeasurement: Bool = false) throws -> CardHistoryPage {
    var data = try historyGolden("page")
    data[3] = flags
    historyPut(boot, at: 8, in: &data); historyPut(first, at: 16, in: &data)
    for index in 0..<6 {
        historyPut(first + UInt32(index), at: 32 + index * 68, in: &data)
        if let unix { historyPut(unix == 0 ? 0 : unix + UInt64(index), at: 40 + index * 68, in: &data) }
    }
    if changeMeasurement { data[48] ^= 1 }
    historyPut(UInt32(0), at: 24, in: &data)
    historyPut(CardImageProtocol.crc32(data), at: 24, in: &data)
    return try CardHistoryPage(packet: data)
}

struct CardHistoryTests {
    private func temporaryURL() -> URL {
        FileManager.default.temporaryDirectory.appendingPathComponent("env-history-" + UUID().uuidString)
            .appendingPathComponent("readings.sqlite")
    }

    @Test func swiftMatchesIndependentCAndPythonGoldenHistoryAndClock() throws {
        let page = try CardHistoryPage(packet: historyGolden("page"))
        #expect(page.requestID == 0x12345678 && page.bootID == 0x9abcdef012345678)
        #expect(page.records.count == 6 && page.cursor.sequence == 1006 && page.clockRevision == 7)
        let record = try CardHistoryRecord(raw: historyGolden("record"))
        let values = record.reading(receivedAt: .now)
        #expect(values.value(sensor: "stcc4", field: "temperature_mc") == -12345)
        #expect(values.value(sensor: "sfa40", field: "hcho_millippb") == -123)
        #expect(abs(try #require(values.formaldehydePPM) + 0.000123) < 1e-12)
        #expect(values.sensors["sfa40"]?.values["ready"] == nil)
        #expect(values.value(sensor: "sgp41", field: "raw_nox") == 65535)
        let limits = try CardHistoryRecord(raw: historyGolden("signed_limits")).reading(receivedAt: .now)
        #expect(limits.sequence == UInt32.max && limits.uptimeMilliseconds == UInt64(UInt32.max) * 1000)
        #expect(limits.value(sensor: "stcc4", field: "co2_ppm") == -32768)
        #expect(limits.value(sensor: "stcc4", field: "temperature_mc") == Double(Int32.min))
        #expect(limits.value(sensor: "opt4001", field: "millilux") == Double(UInt32.max))
        let status = try CardHistoryStatus(packet: historyGolden("status"))
        #expect(status.hasGap && status.hasMore && status.pageBytes == 440 && status.currentBootID == 0x1122334455667788)
        #expect(try CardHistoryStatus(packet: historyGolden("error_status")).error == -5)
        let clock = try CardDeviceClock(packet: historyGolden("clock_state"))
        #expect(clock == status.deviceClock && clock.known)
        #expect(try CardDeviceClock(packet: historyGolden("clock_unknown")).date(uptimeMilliseconds: 300000) == nil)
        #expect(try EnvSensorProtocol.encodeClockSync(unixMilliseconds: 1780000300123) == historyGolden("clock_write"))
        #expect(try CardHistoryProtocol.control(operation: 1, requestID: 0x12345678, cursor: .beginning) == historyGolden("pull_oldest"))
        #expect(try CardHistoryProtocol.control(operation: 1, requestID: 0xa1b2c3d4,
            cursor: .init(bootID: 0x8fedcba987654321, sequence: UInt32.max)) == historyGolden("pull_after"))
        #expect(try CardHistoryProtocol.control(operation: 2, requestID: 0x12345678,
            cursor: .init(bootID: 0x11, sequence: 0x22)) == historyGolden("ack"))
        #expect(try CardHistoryProtocol.control(operation: 3, requestID: 0x12345678, cursor: .beginning) == historyGolden("cancel"))
        let canonical = try CardHistoryRecord(raw: historyGolden("canonical")).reading(receivedAt: .now)
        #expect(canonical.value(sensor: "stcc4", field: "humidity_mpercent") == nil)
        #expect(canonical.sensors["stcc4"]?.values["self_test_result"] == .integer(0x55aa))
        #expect(canonical.formaldehydePPM == nil)
        var invalid = try historyGolden("canonical"); invalid[20] = 1
        #expect(throws: CardHistoryError.invalidRecord) { try CardHistoryRecord(raw: invalid) }
    }

    @Test func crcCorruptionIsRejectedWhileSequenceGapsRemainExplicit() throws {
        let gap = try CardHistoryPage(packet: historyGolden("page_gap"))
        #expect(gap.flags == 3 && gap.cursor.sequence == 1007)
        var corrupted = try historyGolden("page")
        corrupted[48] ^= 1
        #expect(throws: CardHistoryError.invalidCRC) { try CardHistoryPage(packet: corrupted) }
        var unreportedGap = try historyGolden("page_gap")
        unreportedGap[3] = 1
        historyPut(UInt32(0), at: 24, in: &unreportedGap)
        historyPut(CardImageProtocol.crc32(unreportedGap), at: 24, in: &unreportedGap)
        #expect(throws: CardHistoryError.invalidRecord) { try CardHistoryPage(packet: unreportedGap) }
        for operation: UInt8 in [1, 2, 3] {
            #expect(throws: CardHistoryError.invalidCursor) {
                try CardHistoryProtocol.control(operation: operation, requestID: 1, cursor: .init(bootID: 1, sequence: 0))
            }
        }
        #expect(throws: CardHistoryError.invalidCursor) { try CardHistoryProtocol.control(operation: 2, requestID: 1, cursor: .beginning) }
        #expect(throws: CardHistoryError.invalidCursor) { try CardHistoryProtocol.control(operation: 3, requestID: 1, cursor: .init(bootID: 1, sequence: 1)) }
    }

    @Test func durablePageAndCursorAreVisibleBeforeACKAndReplaySurvivesProcessRestart() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let page = try historyPage()
        let received = Date(timeIntervalSince1970: 1790000000)
        do {
            let store = try SensorStore(url: url)
            #expect(try store.saveHistoryPage(page, deviceID: "card", expectedCursor: .beginning, receivedAt: received) == 6)
            let independentlyOpened = try SensorStore(url: url)
            #expect(try independentlyOpened.counts().total == 6)
            #expect(try independentlyOpened.historyCursor(deviceID: "card") == page.cursor)
        }
        let reopened = try SensorStore(url: url)
        #expect(try reopened.saveHistoryPage(page, deviceID: "card", expectedCursor: .beginning,
            receivedAt: received.addingTimeInterval(300)) == 0)
        #expect(try reopened.historyCursor(deviceID: "card") == page.cursor)
        #expect(try reopened.history(deviceID: "card").allSatisfy { $0.reading.receivedAt == received })
    }

    @Test func failedMidPageCommitLeavesNeitherMeasurementsNorACKCursor() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        var database: OpaquePointer?
        #expect(sqlite3_open(url.path, &database) == SQLITE_OK)
        defer { sqlite3_close(database) }
        #expect(sqlite3_exec(database, "CREATE TRIGGER simulate_full_disk BEFORE INSERT ON samples WHEN NEW.sequence=1003 BEGIN SELECT RAISE(ABORT,'disk full'); END", nil, nil, nil) == SQLITE_OK)
        #expect(throws: SensorStoreError.self) { try store.saveHistoryPage(historyPage(), deviceID: "card", expectedCursor: .beginning) }
        #expect(try store.counts().total == 0)
        #expect(try store.historyCursor(deviceID: "card") == .beginning)
    }

    @Test func timestampCorrectionDeduplicatesButChangedMeasurementIsRejected() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let unknown = try historyPage(unix: 0)
        try store.saveHistoryPage(unknown, deviceID: "card", expectedCursor: .beginning)
        #expect(try store.chartReadings(deviceID: "card", since: .distantPast).isEmpty)
        #expect(try store.timeSummary(deviceID: "card", since: .distantPast, until: .distantFuture).unknownTimeCount == 6)
        let corrected = try historyPage(unix: 1780000000)
        #expect(try store.saveHistoryPage(corrected, deviceID: "card", expectedCursor: unknown.cursor) == 0)
        #expect(try store.counts().total == 6)
        #expect(try store.timeSummary(deviceID: "card", since: .distantPast, until: .distantFuture).unknownTimeCount == 0)
        #expect(try store.history(deviceID: "card").allSatisfy { $0.reading.measuredAt != nil && $0.reading.rawFrame == unknown.records[Int($0.reading.sequence - 1001)].raw })
        #expect(throws: SensorStoreError.self) {
            try store.saveHistoryPage(historyPage(changeMeasurement: true), deviceID: "card", expectedCursor: corrected.cursor)
        }
        #expect(try store.counts().total == 6)
    }

    @Test func newerClockCorrectsOnlyItsBootWithoutChangingReceiptOrRawMeasurement() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let first = try historyPage(boot: 1, unix: 0)
        let second = try historyPage(boot: 2, unix: 0)
        let received = Date(timeIntervalSince1970: 1790000000)
        try store.saveHistoryPage(first, deviceID: "card", expectedCursor: .beginning, receivedAt: received)
        try store.saveHistoryPage(second, deviceID: "card", expectedCursor: first.cursor, receivedAt: received)
        let clock = CardDeviceClock(bootID: 2, revision: 2, unixAnchorMilliseconds: 1780000300000, uptimeAnchorMilliseconds: 600000)
        try store.applyDeviceClock(clock, deviceID: "card")
        try store.applyDeviceClock(.init(bootID: 2, revision: 1, unixAnchorMilliseconds: 1781000300000,
            uptimeAnchorMilliseconds: 600000), deviceID: "card")
        let rows = try store.history(deviceID: "card")
        #expect(rows.filter { $0.bootID == first.cursor.bootKey }.allSatisfy { $0.reading.measuredAt == nil })
        #expect(rows.filter { $0.bootID == second.cursor.bootKey }.first?.reading.measuredAt == Date(timeIntervalSince1970: 1780000005))
        #expect(rows.allSatisfy { $0.reading.receivedAt == received && $0.reading.rawFrame == first.records[Int($0.reading.sequence - 1001)].raw })
    }

    @Test func backlogCannotReplaceLatestAndRangeExportsUseMeasuredTime() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let current = try historyPage(boot: 2, unix: 1790000000)
        let old = try historyPage(boot: 1, unix: 1780000000)
        try store.saveHistoryPage(current, deviceID: "card", expectedCursor: .beginning)
        try store.saveHistoryPage(old, deviceID: "card", expectedCursor: .beginning)
        #expect(try store.latestReadings().first?.bootID == current.cursor.bootKey)
        let since = Date(timeIntervalSince1970: 1790000001)
        let until = Date(timeIntervalSince1970: 1790000003)
        #expect(try store.chartReadings(deviceID: "card", since: since, until: until, bucketSeconds: 1).map(\.reading.sequence) == [1002,1003,1004])
        let decoder = JSONDecoder(); decoder.dateDecodingStrategy = .millisecondsSince1970
        let export = try decoder.decode(ReadingExportBatch.self, from: store.exportJSON(deviceID: "card", since: since, until: until))
        #expect(export.readings.map(\.reading.sequence) == [1002,1003,1004])
        #expect(try store.timeSummary(deviceID: "card", since: since, until: until).measuredCount == 3)
        #expect(try store.historyCursor(deviceID: "card") == current.cursor)
    }

    @Test func gapRescanConvergesWithoutNormalReplayRewindingDurableCursor() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let prior = try historyPage(first: 2001)
        try store.saveHistoryPage(prior, deviceID: "card", expectedCursor: .beginning)
        var walk = CardHistoryResumePosition()
        let earliest = try historyPage(first: 1001, flags: 3)
        try store.saveHistoryPage(earliest, deviceID: "card", expectedCursor: prior.cursor)
        #expect(try store.historyCursor(deviceID: "card") == prior.cursor)
        walk.acknowledged(earliest.cursor, hasMore: true)
        #expect(walk.requestCursor(durable: prior.cursor) == earliest.cursor)
        #expect(CardHistoryResumePosition().requestCursor(durable: prior.cursor) == prior.cursor)
        let lastRetained = try historyPage(first: 1007, flags: 2)
        try store.saveHistoryPage(lastRetained, deviceID: "card", expectedCursor: prior.cursor, completesGapRescan: true)
        #expect(try store.historyCursor(deviceID: "card") == lastRetained.cursor)
        let newBoot = try historyPage(boot: 3, first: 1, flags: 0)
        try store.saveHistoryPage(newBoot, deviceID: "card", expectedCursor: lastRetained.cursor)
        #expect(try store.historyCursor(deviceID: "card") == newBoot.cursor)
        try store.saveHistoryPage(earliest, deviceID: "card", expectedCursor: prior.cursor)
        #expect(try store.historyCursor(deviceID: "card") == newBoot.cursor)
        try store.finishEmptyHistoryRescan(deviceID: "card", expectedCursor: prior.cursor)
        #expect(try store.historyCursor(deviceID: "card") == newBoot.cursor)
        try store.finishEmptyHistoryRescan(deviceID: "card", expectedCursor: newBoot.cursor)
        #expect(try store.historyCursor(deviceID: "card") == .beginning)
    }

    @Test func gapWithAnExactFullLastPageCanFinishOnEmptyComplete() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let missingSuffix = try historyPage(first: 2001)
        try store.saveHistoryPage(missingSuffix, deviceID: "card", expectedCursor: .beginning)
        let fullLastPage = try historyPage(first: 1001, flags: 3)
        try store.saveHistoryPage(fullLastPage, deviceID: "card", expectedCursor: missingSuffix.cursor)
        #expect(try store.historyCursor(deviceID: "card") == missingSuffix.cursor)
        try store.finishHistoryRescan(deviceID: "card", expectedCursor: missingSuffix.cursor, finalCursor: fullLastPage.cursor)
        #expect(try store.historyCursor(deviceID: "card") == fullLastPage.cursor)
        #expect(throws: CardHistoryError.invalidCursor) {
            try store.finishHistoryRescan(deviceID: "card", expectedCursor: fullLastPage.cursor,
                finalCursor: .init(bootID: fullLastPage.bootID, sequence: 500))
        }
        #expect(try store.historyCursor(deviceID: "card") == fullLastPage.cursor)
    }

    @Test func retainedBacklogResumesAfterReopenWithoutDuplicateOrReceiptTimeReordering() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        for process in 0..<2 {
            let store = try SensorStore(url: url)
            for pageNumber in (process * 60)..<((process + 1) * 60) {
                let page = try historyPage(first: 1001 + UInt32(pageNumber * 6), unix: 1780000000 + UInt64(pageNumber * 6))
                let before = try store.historyCursor(deviceID: "card")
                #expect(try store.saveHistoryPage(page, deviceID: "card", expectedCursor: before) == 6)
            }
        }
        let reopened = try SensorStore(url: url)
        #expect(try reopened.counts().total == 720)
        #expect(try reopened.historyCursor(deviceID: "card").sequence == 1720)
        #expect(try reopened.latestReadings().first?.reading.measuredAt == Date(timeIntervalSince1970: 1780000719))
        #expect(try reopened.saveHistoryPage(historyPage(), deviceID: "card", expectedCursor: .beginning) == 0)
        #expect(try reopened.historyCursor(deviceID: "card").sequence == 1720)
        #expect(try reopened.chartReadings(deviceID: "card", since: Date(timeIntervalSince1970: 1780000000), bucketSeconds: 60).count <= 13)
    }

    @Test func impossibleAnchorDatesStayUnknownRatherThanEscapingTheSupportedUTCWindow() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let page = try historyPage(boot: 1, unix: 0)
        try store.saveHistoryPage(page, deviceID: "card", expectedCursor: .beginning)
        let clock = CardDeviceClock(bootID: 1, revision: 1,
            unixAnchorMilliseconds: GeneratedClockSchema.utcMinimumMs, uptimeAnchorMilliseconds: 1000000)
        try store.applyDeviceClock(clock, deviceID: "card")
        #expect(clock.date(uptimeMilliseconds: 300000) == nil)
        #expect(try store.history(deviceID: "card").allSatisfy { $0.reading.measuredAt == nil })
    }

    @MainActor @Test func livePreviewCanAuthenticateWithoutDoubleSavingHistoryOrInventingUTC() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let receiver = SensorPacketReceiver(store: store)
        let reading = try fixtureReading()
        #expect(try receiver.receive(reading.rawFrame, deviceID: "card", persist: false) == nil)
        #expect(receiver.liveReadings["card"]?.rawFrame == reading.rawFrame)
        #expect(try store.counts().total == 0)
        let unknown = try CardHistoryRecord(raw: historyGolden("unknown_time")).reading(receivedAt: .now)
        #expect(unknown.chartTimestamp == nil && unknown.measuredAt == nil)
        #expect(reading.withMeasuredAt(nil, measurementSource: "card-live-v1").chartTimestamp == nil)
        #expect(reading.chartTimestamp == reading.receivedAt)
    }
}
