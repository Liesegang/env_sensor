import CoreBluetooth
import Foundation
import Testing
@testable import EnvSensorCore

@MainActor
struct PacketReceiverTests {
    private func temporaryURL() -> URL {
        FileManager.default.temporaryDirectory.appendingPathComponent("env-sensor-packets-" + UUID().uuidString)
            .appendingPathComponent("readings.sqlite")
    }

    @Test func restoredReadRecoversMissedHeadAndValidatesDelayedNotifyTails() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let receiver = SensorPacketReceiver(store: store)
        let frame = try fixtureReading().rawFrame
        // iOS can restore an already-notifying connection after its first OF
        // packet. The initial Read supplies the validated complete snapshot.
        #expect(try receiver.receive(frame, deviceID: "card") != nil)
        #expect(try receiver.receive(fragment(frame, offset: 30, count: 30), deviceID: "card") == nil)
        #expect(try receiver.receive(fragment(frame, offset: 60), deviceID: "card") == nil)
        #expect(try store.counts().total == 1)
        var corruptTail = fragment(frame, offset: 30)
        corruptTail[12] ^= 1
        #expect(throws: SensorProtocolError.fragmentConflict) {
            try receiver.receive(corruptTail, deviceID: "card")
        }
        #expect(try store.counts().total == 1)
    }

    @Test func missingNotificationIsNotSavedAndLatestReadCanRecoverWithoutDuplicates() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let receiver = SensorPacketReceiver(store: store)
        let frame = try fixtureReading().rawFrame
        #expect(try receiver.receive(fragment(frame, count: 30), deviceID: "card") == nil)
        #expect(throws: SensorProtocolError.fragmentMissing) {
            try receiver.receive(fragment(frame, offset: 60), deviceID: "card")
        }
        #expect(try store.counts().total == 0)
        #expect(try receiver.receive(frame, deviceID: "card") != nil)
        #expect(try receiver.receive(fragment(frame, offset: 60), deviceID: "card") == nil)
        #expect(try store.counts().total == 1)
        #expect(SensorProtocolError.fragmentMissing.canRecoverByReadingLatest)
        #expect(SensorProtocolError.fragmentTimeout.canRecoverByReadingLatest)
        #expect(!SensorProtocolError.fragmentConflict.canRecoverByReadingLatest)
        #expect(!SensorProtocolError.invalidFlags.canRecoverByReadingLatest)
    }

    @Test func readDoesNotDiscardPartialNotifyAndCommitSurvivesReopen() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let receiver = SensorPacketReceiver(store: store)
        let reading = try fixtureReading()
        #expect(try receiver.receive(fragment(reading.rawFrame, count: 30), deviceID: "card") == nil)
        let saved = try #require(try receiver.receive(reading.rawFrame, deviceID: "card"))
        // A separate SQLite connection observes the commit before receive returns.
        let reopened = try SensorStore(url: url)
        #expect(try reopened.latestReadings().first?.id == saved.id)
        #expect(try receiver.receive(fragment(reading.rawFrame, offset: 30), deviceID: "card") == nil)
        #expect(try store.counts().total == 1)
    }

    @Test func delayedOlderReadCannotInventBootButDisconnectAllowsRealReboot() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let receiver = SensorPacketReceiver(store: store)
        let older = try fixtureReading()
        let newer = try fixtureReading(sequence: 2, uptime: 65_000)
        let first = try #require(try receiver.receive(fragment(newer.rawFrame), deviceID: "card"))
        #expect(try receiver.receive(older.rawFrame, deviceID: "card") == nil)
        #expect(try store.counts().total == 1)
        #expect(try store.latestReadings().first?.bootID == first.bootID)
        receiver.connectionEnded(for: "card")
        let reboot = try #require(try receiver.receive(older.rawFrame, deviceID: "card"))
        #expect(reboot.bootID != first.bootID)
        #expect(try store.counts().total == 2)
    }

    @Test func connectionEndedDropsPartialFrameAndFreshStreamWorks() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let receiver = SensorPacketReceiver(store: store)
        let frame = try fixtureReading().rawFrame
        #expect(try receiver.receive(fragment(frame, count: 30), deviceID: "card") == nil)
        receiver.connectionEnded(for: "card")
        #expect(throws: SensorProtocolError.fragmentMissing) {
            try receiver.receive(fragment(frame, offset: 30), deviceID: "card")
        }
        #expect(try receiver.receive(fragment(frame, count: 30), deviceID: "card") == nil)
        #expect(try receiver.receive(fragment(frame, offset: 30), deviceID: "card")?.reading.rawFrame == frame)
        #expect(try store.counts().total == 1)
    }

    @Test func multipleDevicesKeepAssembliesAndOrderingIndependent() throws {
        let url = temporaryURL()
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
        let store = try SensorStore(url: url)
        let receiver = SensorPacketReceiver(store: store)
        let newer = try fixtureReading(sequence: 2, uptime: 65_000).rawFrame
        let older = try fixtureReading().rawFrame
        #expect(try receiver.receive(fragment(newer, count: 30), deviceID: "card") == nil)
        #expect(try receiver.receive(fragment(older, count: 40), deviceID: "outdoor") == nil)
        #expect(try receiver.receive(fragment(newer, offset: 30), deviceID: "card")?.reading.sequence == 2)
        #expect(try receiver.receive(fragment(older, offset: 40), deviceID: "outdoor")?.reading.sequence == 1)
        #expect(try receiver.receive(older, deviceID: "card") == nil)
        #expect(try store.counts().samplesByDevice == ["card": 1, "outdoor": 1])
    }

    @Test func failureGuidancePausesAuthenticationWithoutClassifyingNormalSleepAsFailure() {
        for code in [CBATTError.Code.insufficientAuthentication, .insufficientAuthorization,
                     .insufficientEncryption, .insufficientEncryptionKeySize] {
            let failure = BluetoothFailure(NSError(domain: CBATTErrorDomain, code: code.rawValue))
            #expect(failure.requiresUserAction)
            #expect(failure.message.contains("6桁"))
        }
        let mismatch = BluetoothFailure(NSError(domain: CBErrorDomain,
                                                code: CBError.Code.peerRemovedPairingInformation.rawValue))
        #expect(mismatch.requiresUserAction)
        #expect(mismatch.message.contains("一致"))
        for code in [CBError.Code.peripheralDisconnected, .connectionTimeout, .connectionFailed] {
            #expect(!BluetoothFailure(NSError(domain: CBErrorDomain, code: code.rawValue)).requiresUserAction)
        }
    }

    @Test func staleGATTFailureCanRediscoverWithoutDeletingBondAndIncludesOperation() {
        for code in [CBATTError.Code.writeNotPermitted, .readNotPermitted, .invalidHandle, .attributeNotFound] {
            let failure = BluetoothFailure(NSError(domain: CBATTErrorDomain, code: code.rawValue))
            #expect(failure.canRediscoverServices)
            #expect(!failure.requiresUserAction)
            #expect(failure.message(operation: "通知の登録").hasPrefix("通知の登録:"))
        }
        let authentication = BluetoothFailure(NSError(domain: CBATTErrorDomain,
            code: CBATTError.Code.insufficientAuthentication.rawValue))
        #expect(!authentication.canRediscoverServices)
        #expect(authentication.requiresUserAction)
    }
}
