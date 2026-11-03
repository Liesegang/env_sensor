import CoreBluetooth
import Foundation
import Observation

/// Uses the collector's already authenticated peripheral and delegate. Every
/// next chunk waits for the device's durable offset, not only its ATT response.
@MainActor
@Observable
public final class CardImageTransfer {
    public private(set) var phase: CardImageTransferPhase = .idle
    public private(set) var deviceID: String?
    public private(set) var isStopping = false
    public var isBusy: Bool { phase.isBusy || isStopping }
    public var canCancel: Bool { phase == .waitingConnection || (phase.isBusy && boundary.canCancel && !isStopping) }
    @ObservationIgnored var disconnectRequested: (@MainActor (String) -> Void)?
    @ObservationIgnored var becameIdle: (@MainActor (String) -> Void)?
    @ObservationIgnored private var peripheral: CBPeripheral?
    @ObservationIgnored private var control: CBCharacteristic?
    @ObservationIgnored private var data: CBCharacteristic?
    @ObservationIgnored private var status: CBCharacteristic?
    @ObservationIgnored private var transferID: UInt32 = 0
    @ObservationIgnored private var boundary = CardImageCommitBoundary()
    private var hasCommitted: Bool { boundary.submitted }
    @ObservationIgnored private var expectedCRC: UInt32?
    @ObservationIgnored private var fence = CardImageWriteFence()
    @ObservationIgnored private var queue = CardImageUploadQueue()
    @ObservationIgnored private var stoppingDeviceID: String?
    @ObservationIgnored private var token: UUID?
    @ObservationIgnored private var operation: Task<Void, Never>?
    @ObservationIgnored private var timeout: Task<Void, Never>?
    @ObservationIgnored private var serviceWaiter: CheckedContinuation<Void, any Error>?
    @ObservationIgnored private var writeWaiter: CheckedContinuation<Void, any Error>?
    @ObservationIgnored private var writeUUID: CBUUID?
    @ObservationIgnored private var readWaiter: CheckedContinuation<CardImageStatus, any Error>?

    private static let serviceUUID = CBUUID(string: CardImageProtocol.serviceUUID)
    private static let controlUUID = CBUUID(string: CardImageProtocol.controlUUID)
    private static let dataUUID = CBUUID(string: CardImageProtocol.dataUUID)
    private static let statusUUID = CBUUID(string: CardImageProtocol.statusUUID)

    public init() {}

    public func enqueue(frame: Data, deviceID: String) {
        guard !phase.isBusy else { return }
        do { try queue.enqueue(frame, deviceID: deviceID) }
        catch { phase = .failed(error.localizedDescription); return }
        self.deviceID = deviceID
        boundary.begin()
        phase = .waitingConnection
    }

    public func startQueued(on target: CBPeripheral, authenticated: Bool, foreground: Bool) {
        guard phase == .waitingConnection, target.state == .connected,
              let frame = queue.take(for: target.identifier.uuidString, authenticated: authenticated,
                                     foreground: foreground, fenceReady: !isStopping && fence.canStart) else { return }
        phase = .idle
        begin(frame: frame, on: target)
    }

    public func failWaiting(for id: String, message: String) {
        guard phase == .waitingConnection, deviceID == id else { return }
        queue.cancel()
        phase = .failed(message)
    }

    func servicesInvalidated(_ id: String) {
        guard deviceID == id else { return }
        if phase == .waitingConnection { return }
        if phase.isBusy {
            if hasCommitted { pauseAfterCommit() }
            else { cancel(reason: "cardのサービス情報が更新されたため転送を中止しました。再接続後に送り直してください。") }
        }
        control = nil; data = nil; status = nil
    }

    public func begin(frame: Data, on peripheral: CBPeripheral) {
        guard !isBusy, fence.canStart else { return }
        guard peripheral.state == .connected else { phase = .failed(CardImageError.notConnected.localizedDescription); return }
        do { try CardImageProtocol.validateFrame(frame) }
        catch { phase = .failed(error.localizedDescription); return }
        self.peripheral = peripheral
        deviceID = peripheral.identifier.uuidString
        transferID = UInt32.random(in: 1...UInt32.max)
        boundary.begin()
        expectedCRC = CardImageProtocol.crc32(frame)
        let operationToken = UUID()
        token = operationToken
        phase = .preparing
        operation = Task { [weak self] in
            guard let self else { return }
            do {
                try await self.run(frame: frame, peripheral: peripheral)
                guard self.token == operationToken else { return }
                self.phase = .complete
            } catch {
                guard self.token == operationToken else { return }
                if !self.hasCommitted { self.stopBeforeCommit() }
                self.phase = .failed(error.localizedDescription)
            }
            if self.token == operationToken { self.finish() }
        }
    }

    public func cancel(reason: String = "画像の転送を中止しました。") {
        guard phase.isBusy else { return }
        if phase == .waitingConnection {
            queue.cancel()
            phase = .cancelled(reason)
            return
        }
        if hasCommitted { pauseAfterCommit(); return }
        token = nil
        operation?.cancel()
        stopBeforeCommit()
        phase = .cancelled(reason)
        finish()
    }

    public func foregroundChanged(_ foreground: Bool) {
        if !foreground { cancel(reason: "アプリを開いたまま画像を送信してください。転送を中止しました。") }
    }

    public func resumeDisplayResult(on target: CBPeripheral) {
        guard phase == .waitingForDisplay, !isStopping, fence.canStart, target.identifier.uuidString == deviceID,
              target.state == .connected, let expectedCRC else { return }
        peripheral = target
        let operationToken = UUID()
        token = operationToken
        phase = .displaying
        operation = Task { [weak self] in
            guard let self else { return }
            do {
                try await self.discover(target)
                _ = try await self.waitForStatus(crc: expectedCRC, timeoutSeconds: 90, pollMilliseconds: 500) {
                    $0.state == 5 && $0.received == CardImageProtocol.frameLength
                }
                guard self.token == operationToken else { return }
                self.phase = .complete
            } catch {
                guard self.token == operationToken else { return }
                self.phase = .failed(error.localizedDescription)
            }
            if self.token == operationToken { self.finish() }
        }
    }

    public func disconnected(_ id: String) {
        if stoppingDeviceID == id {
            fence.disconnected()
            isStopping = false
            stoppingDeviceID = nil
        }
        guard deviceID == id else { return }
        if phase == .waitingConnection { return }
        guard phase.isBusy else { return }
        if hasCommitted { pauseAfterCommit(connectionEnded: true); peripheral = nil; return }
        token = nil
        operation?.cancel()
        failWaiters(CardImageError.notConnected)
        phase = .failed("cardとの接続が切れました。画像を送り直してください。以前の画像は保持されます。")
        finish()
    }

    @discardableResult
    func discoveredServices(_ target: CBPeripheral, error: (any Error)?) -> Bool {
        guard matches(target), serviceWaiter != nil else { return false }
        if let error { failWaiters(CardImageError.operation("画像サービスの検索", BluetoothFailure(error).message)) }
        else if let service = target.services?.first(where: { $0.uuid == Self.serviceUUID }) {
            target.discoverCharacteristics([Self.controlUUID, Self.dataUUID, Self.statusUUID], for: service)
        } else { failWaiters(CardImageError.unsupported) }
        return true
    }

    @discardableResult
    func discoveredCharacteristics(_ target: CBPeripheral, service: CBService, error: (any Error)?) -> Bool {
        guard service.uuid == Self.serviceUUID else { return false }
        guard matches(target), serviceWaiter != nil else { return true }
        if let error {
            failWaiters(CardImageError.operation("画像の特性の検索", BluetoothFailure(error).message))
            return true
        }
        control = service.characteristics?.first { $0.uuid == Self.controlUUID && $0.properties.contains(.write) }
        data = service.characteristics?.first { $0.uuid == Self.dataUUID && $0.properties.contains(.write) }
        status = service.characteristics?.first { $0.uuid == Self.statusUUID && $0.properties.contains(.read) }
        if control != nil, data != nil, status != nil {
            timeout?.cancel()
            let continuation = serviceWaiter
            serviceWaiter = nil
            continuation?.resume()
        } else { failWaiters(CardImageError.unsupported) }
        return true
    }

    @discardableResult
    func wrote(_ target: CBPeripheral, characteristic: CBCharacteristic, error: (any Error)?) -> Bool {
        guard characteristic.uuid == Self.controlUUID || characteristic.uuid == Self.dataUUID else { return false }
        if target.identifier.uuidString == stoppingDeviceID, fence.acknowledge(characteristic.uuid.uuidString) { return true }
        guard matches(target), characteristic.uuid == writeUUID, let continuation = writeWaiter else { return true }
        timeout?.cancel()
        writeWaiter = nil
        writeUUID = nil
        if let error {
            let operation = characteristic.uuid == Self.dataUUID ? "画像データの書き込み" : hasCommitted ? "画像の確定" : "画像転送の開始"
            continuation.resume(throwing: CardImageError.operation(operation, BluetoothFailure(error).message))
        }
        else { continuation.resume() }
        return true
    }

    @discardableResult
    func received(_ target: CBPeripheral, characteristic: CBCharacteristic, error: (any Error)?) -> Bool {
        guard characteristic.uuid == Self.statusUUID else { return false }
        guard matches(target), let continuation = readWaiter else { return true }
        timeout?.cancel()
        readWaiter = nil
        do {
            if let error { throw CardImageError.operation("画像の状態の読取", BluetoothFailure(error).message) }
            guard let packet = characteristic.value else { throw CardImageError.invalidStatus }
            continuation.resume(returning: try CardImageStatus(packet: packet))
        } catch { continuation.resume(throwing: error) }
        return true
    }

    private func run(frame: Data, peripheral: CBPeripheral) async throws {
        try Task.checkCancellation()
        try await discover(peripheral)
        guard let control, let data else { throw CardImageError.unsupported }
        let crc = expectedCRC ?? CardImageProtocol.crc32(frame)
        try await write(try CardImageProtocol.begin(id: transferID, frame: frame), characteristic: control)
        _ = try await waitForStatus(crc: crc, timeoutSeconds: 90) { $0.state == 1 && $0.received == 0 }
        let capacity = peripheral.maximumWriteValueLength(for: .withResponse)
        var offset = 0
        while offset < frame.count {
            try Task.checkCancellation()
            let chunk = try CardImageProtocol.chunk(id: transferID, frame: frame, offset: offset, capacity: capacity)
            let targetOffset = offset + chunk.count - 12
            try await write(chunk, characteristic: data)
            _ = try await waitForStatus(crc: crc, timeoutSeconds: 90) {
                $0.state == 1 && $0.received == targetOffset
            }
            offset = targetOffset
            phase = .sending(Double(offset) / Double(frame.count))
        }
        boundary.submit()
        phase = .displaying
        try await write(CardImageProtocol.commit(id: transferID), characteristic: control)
        _ = try await waitForStatus(crc: crc, timeoutSeconds: 90, pollMilliseconds: 500) {
            $0.state == 5 && $0.received == CardImageProtocol.frameLength
        }
    }

    private func discover(_ target: CBPeripheral) async throws {
        try Task.checkCancellation()
        try await withCheckedThrowingContinuation { continuation in
            serviceWaiter = continuation
            startTimeout()
            target.discoverServices([Self.serviceUUID])
        }
    }

    private func write(_ packet: Data, characteristic: CBCharacteristic) async throws {
        try Task.checkCancellation()
        guard let peripheral, peripheral.state == .connected else { throw CardImageError.notConnected }
        try await withCheckedThrowingContinuation { continuation in
            writeWaiter = continuation
            writeUUID = characteristic.uuid
            startTimeout()
            peripheral.writeValue(packet, for: characteristic, type: .withResponse)
        }
    }

    private func readStatus() async throws -> CardImageStatus {
        try Task.checkCancellation()
        guard let peripheral, let status, peripheral.state == .connected else { throw CardImageError.notConnected }
        return try await withCheckedThrowingContinuation { continuation in
            readWaiter = continuation
            startTimeout()
            // Reads are serialized. No status subscription is needed, avoiding
            // ambiguity between Read completions and Notify delegate callbacks.
            peripheral.readValue(for: status)
        }
    }

    private func waitForStatus(crc: UInt32, timeoutSeconds: Double, pollMilliseconds: Int = 20,
                               ready: (CardImageStatus) -> Bool) async throws -> CardImageStatus {
        let deadline = ContinuousClock.now.advanced(by: .seconds(timeoutSeconds))
        while ContinuousClock.now < deadline {
            let response = try await readStatus()
            if response.id == transferID {
                if response.state == 3 { throw CardImageError.device(response.error) }
                guard response.total == CardImageProtocol.frameLength, response.crc == crc else {
                    throw CardImageError.invalidStatus
                }
                if ready(response) { return response }
            }
            try await Task.sleep(for: .milliseconds(pollMilliseconds))
        }
        throw CardImageError.timedOut
    }

    private func startTimeout() {
        timeout?.cancel()
        timeout = Task { [weak self] in
            do { try await Task.sleep(for: .seconds(10)) }
            catch { return }
            self?.failWaiters(CardImageError.timedOut)
        }
    }

    private func stopBeforeCommit() {
        let target = peripheral, characteristic = control
        fence.stop(outstanding: writeWaiter != nil ? writeUUID?.uuidString : nil,
                   abort: characteristic?.uuid.uuidString)
        stoppingDeviceID = deviceID
        isStopping = true
        failWaiters(CardImageError.cancelled)
        if let target, let characteristic, target.state == .connected,
           let abort = boundary.abort(id: transferID) {
            target.writeValue(abort, for: characteristic, type: .withResponse)
        }
        // This boundary prevents a stale DATA/CONTROL ACK from satisfying a
        // newly started BEGIN, even when cancellation interrupts an ATT write.
        if let deviceID { disconnectRequested?(deviceID) }
    }

    private func pauseAfterCommit(connectionEnded: Bool = false) {
        token = nil
        operation?.cancel()
        let hasPendingWrite = writeWaiter != nil
        if hasPendingWrite && !connectionEnded {
            fence.stop(outstanding: writeUUID?.uuidString, abort: nil)
            stoppingDeviceID = deviceID
            isStopping = true
        }
        failWaiters(CardImageError.cancelled)
        operation = nil
        phase = .waitingForDisplay
        // No ABORT after COMMIT is submitted. The device may already have
        // accepted state 7 and must finish committing/displaying independently.
        if hasPendingWrite && !connectionEnded, let deviceID { disconnectRequested?(deviceID) }
    }

    private func failWaiters(_ error: any Error) {
        timeout?.cancel()
        let service = serviceWaiter, write = writeWaiter, read = readWaiter
        serviceWaiter = nil; writeWaiter = nil; readWaiter = nil; writeUUID = nil
        service?.resume(throwing: error)
        write?.resume(throwing: error)
        read?.resume(throwing: error)
    }

    private func matches(_ target: CBPeripheral) -> Bool {
        phase.isBusy && target.identifier.uuidString == deviceID
    }

    private func finish() {
        timeout?.cancel()
        timeout = nil; operation = nil; token = nil
        peripheral = nil; control = nil; data = nil; status = nil
        if let deviceID { becameIdle?(deviceID) }
    }
}
