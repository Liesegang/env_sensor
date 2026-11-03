import CoreBluetooth
import Foundation
import Observation

/// Event-driven page synchronization. Status hints wake the application; no
/// foreground screen or periodic background timer is required to continue.
@MainActor @Observable
public final class CardHistorySync {
    public private(set) var messages: [String: String] = [:]
    public private(set) var warnings: [String: String] = [:]
    public private(set) var clocks: [String: CardDeviceClock] = [:]
    @ObservationIgnored var saved: (@MainActor (Int) -> Void)?
    @ObservationIgnored var shouldPause: (@MainActor (String) -> Bool)?
    @ObservationIgnored private let store: SensorStore
    @ObservationIgnored private var sessions: [String: Session] = [:]
    private enum Phase { case idle, pull, status, page, ack, failed }
    private final class Session {
        let target: CBPeripheral
        var control: CBCharacteristic?
        var status: CBCharacteristic?
        var data: CBCharacteristic?
        var phase: Phase = .idle
        var clockAcknowledged = false
        var notifyReady = false
        var request: UInt32 = 0
        var before: CardHistoryCursor = .beginning
        var position = CardHistoryResumePosition()
        var gapRescan = false
        var latest: CardHistoryStatus?
        var statusReading = false
        var statusDirty = false
        var pageCursor: CardHistoryCursor?
        init(_ target: CBPeripheral) { self.target = target }
    }
    public static let serviceUUID = CBUUID(string: GeneratedHistorySchema.serviceUuid)
    private static let controlUUID = CBUUID(string: GeneratedHistorySchema.controlUuid)
    private static let statusUUID = CBUUID(string: GeneratedHistorySchema.statusUuid)
    private static let dataUUID = CBUUID(string: GeneratedHistorySchema.dataUuid)
    public init(store: SensorStore) { self.store = store }
    public func supports(_ id: String) -> Bool { sessions[id] != nil }

    func discoveredCapability(_ target: CBPeripheral) {
        let id = target.identifier.uuidString
        guard sessions[id] == nil else { return }
        guard let service = target.services?.first(where: { $0.uuid == Self.serviceUUID }) else { return }
        let session = Session(target)
        sessions[id] = session
        messages[id] = "毎秒の履歴同期を準備中"
        target.discoverCharacteristics([Self.controlUUID, Self.statusUUID, Self.dataUUID], for: service)
    }

    func disconnected(_ id: String) {
        sessions.removeValue(forKey: id)
        clocks.removeValue(forKey: id)
        messages[id] = "次の通信で履歴同期を再開します"
    }
    func clockAcknowledged(_ id: String) {
        guard let session = sessions[id] else { return }
        session.clockAcknowledged = true
        resume(id)
    }
    func clockState(_ clock: CardDeviceClock, deviceID: String) throws {
        try store.applyDeviceClock(clock, deviceID: deviceID)
        if let old = clocks[deviceID], old.bootID == clock.bootID, old.revision >= clock.revision { return }
        clocks[deviceID] = clock
        saved?(0)
    }
    func resume(_ id: String) {
        guard let session = sessions[id], session.phase == .idle, session.clockAcknowledged,
              !session.statusReading,
              session.notifyReady, let control = session.control, session.data != nil,
              shouldPause?(id) != true, session.target.state == .connected else { return }
        do {
            session.before = try store.historyCursor(deviceID: id)
            session.request = UInt32.random(in: 1...UInt32.max)
            session.latest = nil
            session.phase = .pull
            let command = try CardHistoryProtocol.control(operation: GeneratedHistorySchema.Operations.pull,
                requestID: session.request, cursor: session.position.requestCursor(durable: session.before))
            messages[id] = "毎秒の測定履歴を同期中"
            session.target.writeValue(command, for: control, type: .withResponse)
        } catch { fail(id, error) }
    }

    @discardableResult
    func characteristics(_ target: CBPeripheral, service: CBService, error: (any Error)?) -> Bool {
        guard service.uuid == Self.serviceUUID else { return false }
        let id = target.identifier.uuidString
        guard let session = sessions[id] else { return true }
        if let error { fail(id, error); return true }
        session.control = service.characteristics?.first { $0.uuid == Self.controlUUID && $0.properties.contains(.write) }
        session.status = service.characteristics?.first { $0.uuid == Self.statusUUID && $0.properties.contains(.read) && $0.properties.contains(.notify) }
        session.data = service.characteristics?.first { $0.uuid == Self.dataUUID && $0.properties.contains(.read) }
        guard session.control != nil, let status = session.status, session.data != nil else {
            fail(id, CardHistoryError.invalidHeader); return true
        }
        readStatus(session) // Authenticated metadata Read also acquires the FW lease.
        if status.isNotifying { session.notifyReady = true; resume(id) }
        else { target.setNotifyValue(true, for: status) }
        return true
    }
    @discardableResult
    func notificationState(_ target: CBPeripheral, characteristic: CBCharacteristic, error: (any Error)?) -> Bool {
        guard characteristic.uuid == Self.statusUUID else { return false }
        let id = target.identifier.uuidString
        guard let session = sessions[id] else { return true }
        if let error { fail(id, error) }
        else { session.notifyReady = characteristic.isNotifying; resume(id) }
        return true
    }
    @discardableResult
    func wrote(_ target: CBPeripheral, characteristic: CBCharacteristic, error: (any Error)?) -> Bool {
        guard characteristic.uuid == Self.controlUUID else { return false }
        let id = target.identifier.uuidString
        guard let session = sessions[id] else { return true }
        if let error { fail(id, error); return true }
        switch session.phase {
        case .pull:
            session.phase = .status
            readStatus(session)
        case .ack:
            let more = session.latest?.hasMore == true
            if let tail = session.pageCursor { session.position.acknowledged(tail, hasMore: more) }
            if !more { session.gapRescan = false }
            session.phase = .idle
            messages[id] = more ? "履歴の続きを同期中" : "保存済みの履歴まで同期しました"
            if !more { saved?(-1) }
            if more { resume(id) }
        default: break
        }
        return true
    }
    @discardableResult
    func received(_ target: CBPeripheral, characteristic: CBCharacteristic, error: (any Error)?) -> Bool {
        guard characteristic.uuid == Self.statusUUID || characteristic.uuid == Self.dataUUID else { return false }
        let id = target.identifier.uuidString
        guard let session = sessions[id], session.phase != .failed else { return true }
        if let error { fail(id, error); return true }
        guard let packet = characteristic.value else { return true }
        do {
            if characteristic.uuid == Self.statusUUID {
                if packet.count == GeneratedHistorySchema.Hint.size {
                    let b = [UInt8](packet)
                    guard Array(b[0..<2]) == GeneratedHistorySchema.Hint.magic,
                          b[2] == GeneratedHistorySchema.version, b[3] <= GeneratedHistorySchema.States.error else {
                        throw CardHistoryError.invalidHeader
                    }
                    if b.historyU32(GeneratedHistorySchema.Hint.requestId) == session.request {
                        session.statusDirty = true
                        if session.phase == .status { readStatus(session) }
                    }
                    return true
                }
                session.statusReading = false
                let status = try CardHistoryStatus(packet: packet)
                try clockState(status.deviceClock, deviceID: id)
                if session.phase == .idle { resume(id); return true }
                guard status.requestID == session.request, session.phase == .status else { return true }
                if status.hasGap {
                    session.gapRescan = true
                    warnings[id] = "履歴に欠落があるため、実在する最古の記録から再確認しています。"
                }
                if status.state == GeneratedHistorySchema.States.error { throw CardHistoryError.device(status.error) }
                session.latest = status
                switch status.state {
                case GeneratedHistorySchema.States.ready:
                    session.phase = .page
                    if let data = session.data { target.readValue(for: data) }
                case GeneratedHistorySchema.States.complete:
                    if session.gapRescan {
                        if let tail = session.pageCursor {
                            try store.finishHistoryRescan(deviceID: id, expectedCursor: session.before, finalCursor: tail)
                        } else if status.earliest == .beginning && status.latest == .beginning {
                            try store.finishEmptyHistoryRescan(deviceID: id, expectedCursor: session.before)
                        }
                    }
                    session.position = CardHistoryResumePosition()
                    session.gapRescan = false
                    session.phase = .idle
                    messages[id] = "保存済みの履歴まで同期しました"
                    saved?(-1)
                default:
                    if session.statusDirty { readStatus(session) }
                }
            } else {
                guard session.phase == .page, let status = session.latest else { return true }
                let page = try CardHistoryPage(packet: packet)
                guard page.requestID == session.request, page.records.count == status.pageCount,
                      page.raw.count == status.pageBytes, page.cursor.sequence == status.pageLastSequence,
                      UInt32(page.flags) == status.flags else {
                    throw CardHistoryError.invalidHeader
                }
                let inserted = try store.saveHistoryPage(page, deviceID: id, expectedCursor: session.before,
                    completesGapRescan: session.gapRescan && !status.hasMore)
                saved?(inserted)
                session.pageCursor = page.cursor
                let ack = try CardHistoryProtocol.control(operation: GeneratedHistorySchema.Operations.ack,
                    requestID: session.request, cursor: page.cursor)
                session.phase = .ack
                if let control = session.control { target.writeValue(ack, for: control, type: .withResponse) }
            }
        } catch { fail(id, error) }
        return true
    }

    private func readStatus(_ session: Session) {
        guard !session.statusReading, let status = session.status else { return }
        session.statusReading = true
        session.statusDirty = false
        session.target.readValue(for: status)
    }
    private func fail(_ id: String, _ error: any Error) {
        sessions[id]?.phase = .failed
        messages[id] = "履歴同期を中断しました。次の接続で保存位置から再開します。"
        warnings[id] = error.localizedDescription
    }
}
