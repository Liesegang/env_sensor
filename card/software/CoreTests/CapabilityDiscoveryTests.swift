import Foundation
import Testing
@testable import EnvSensorCore

@MainActor struct CapabilityDiscoveryTests {
    private func temporaryURL() -> URL {
        FileManager.default.temporaryDirectory.appendingPathComponent("env-capability-" + UUID().uuidString)
            .appendingPathComponent("readings.sqlite")
    }

    @Test func cachedENVCallbacksWaitUntilHistoryCapabilityIsKnown() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let receiver = SensorPacketReceiver(store: store)
        let frame = try fixtureReading().rawFrame
        let pending = CoreBluetoothCollector.readingDisposition(capabilitiesDiscovered: false, historySupported: false)
        #expect(pending == .awaitingCapabilities)
        if pending != .awaitingCapabilities { _ = try receiver.receive(frame, deviceID: "card", persist: pending == .legacyPersistence) }
        #expect(try store.counts().total == 0)
        let discoveredCard = CoreBluetoothCollector.readingDisposition(capabilitiesDiscovered: true, historySupported: true)
        #expect(discoveredCard == .historyPreview)
        #expect(try receiver.receive(frame, deviceID: "card", persist: discoveredCard == .legacyPersistence) == nil)
        #expect(receiver.liveReadings["card"]?.rawFrame == frame)
        #expect(try store.counts().total == 0)
    }

    @Test func confirmedENVOnlyDeviceStillPersistsLegacyAfterDiscovery() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let receiver = SensorPacketReceiver(store: store)
        let route = CoreBluetoothCollector.readingDisposition(capabilitiesDiscovered: true, historySupported: false)
        #expect(route == .legacyPersistence)
        #expect(try receiver.receive(fixtureReading().rawFrame, deviceID: "outdoor", persist: route == .legacyPersistence) != nil)
        #expect(try store.counts().total == 1)
    }

    @Test func rediscoveryClosesTheRouteEvenIfOldHistoryCapabilitiesRemainCached() {
        #expect(CoreBluetoothCollector.readingDisposition(capabilitiesDiscovered: false, historySupported: true) == .awaitingCapabilities)
        #expect(CoreBluetoothCollector.readingDisposition(capabilitiesDiscovered: false, historySupported: false) == .awaitingCapabilities)
    }
}
