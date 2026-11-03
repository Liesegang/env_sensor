import CoreBluetooth
import Foundation
import Observation
import OSLog

/// Core Bluetooth delegates are delivered on the explicitly selected main queue.
/// The preconcurrency conformances keep those Objective-C callbacks synchronous
/// on MainActor, including the small SQLite commit before background suspension.
@MainActor
@Observable
public final class CoreBluetoothCollector: NSObject {
    public private(set) var availability: BluetoothAvailability = .starting
    public private(set) var isCollecting: Bool
    public private(set) var isScanning = false
    public private(set) var discoveredDevices: [DiscoveredSensor] = []
    public private(set) var connections: [String: SensorConnectionState] = [:]
    public private(set) var lastSavedReading: StoredReading?
    /// Advances after a committed reading or device setting change.
    public private(set) var saveRevision = 0
    public private(set) var lastError: String?
    public private(set) var clockSync: [String: SensorClockSyncState] = [:]
    public let imageTransfer = CardImageTransfer()
    public let historySync: CardHistorySync
    public private(set) var liveReadings: [String: SensorReading] = [:]
    @ObservationIgnored private var liveClockEligible: Set<String> = []

    @ObservationIgnored private let store: SensorStore
    @ObservationIgnored private let receiver: SensorPacketReceiver
    @ObservationIgnored private let defaults: UserDefaults
    @ObservationIgnored private var central: CBCentralManager?
    @ObservationIgnored private var peripherals: [String: CBPeripheral] = [:]
    @ObservationIgnored private var registered: [String: DeviceRecord] = [:]
    @ObservationIgnored private var characteristics: [String: CBCharacteristic] = [:]
    @ObservationIgnored private var initialReads: Set<String> = []
    @ObservationIgnored private var pendingReads: Set<String> = []
    @ObservationIgnored private var clockWrites: [String: Date] = [:]
    @ObservationIgnored private var blockedConnections: Set<String> = []
    @ObservationIgnored private var discoveryRequested = false
    @ObservationIgnored private var isForeground = false
    @ObservationIgnored private var freshCharacteristics: Set<String> = []
    @ObservationIgnored private var serviceDiscovery: Set<String> = []
    @ObservationIgnored private var capabilitiesDiscovered: Set<String> = []
    @ObservationIgnored private var characteristicDiscovery: Set<String> = []
    @ObservationIgnored private var rediscoveryAttempted: Set<String> = []
    @ObservationIgnored private var dedicatedClockDevices: Set<String> = []
    @ObservationIgnored private var clockWriter: [String: CBCharacteristic] = [:]
    @ObservationIgnored private var clockReader: [String: CBCharacteristic] = [:]
    @ObservationIgnored private var authenticatedConnections: Set<String> = []

    private static let collectionPreference = "EnvSensor.collectionEnabled.v1"
    private static let restorationIdentifier = "EnvSensor.central.v1"
    private static let service = CBUUID(string: EnvSensorProtocol.serviceUUID)
    private static let characteristic = CBUUID(string: EnvSensorProtocol.characteristicUUID)
    private static let clockService = CBUUID(string: GeneratedClockSchema.serviceUuid)
    private static let clockWrite = CBUUID(string: GeneratedClockSchema.writeUuid)
    private static let clockState = CBUUID(string: GeneratedClockSchema.stateUuid)
    private static let logger = Logger(subsystem: "com.liesegang.envsensor", category: "BLE")

    public init(store: SensorStore, defaults: UserDefaults = .standard) {
        self.store = store
        receiver = SensorPacketReceiver(store: store)
        historySync = CardHistorySync(store: store)
        self.defaults = defaults
        isCollecting = defaults.object(forKey: Self.collectionPreference) as? Bool ?? true
        super.init()
        imageTransfer.disconnectRequested = { [weak self] id in
            guard let self, let peripheral = self.peripherals[id] else { return }
            self.cancel(peripheral)
        }
        imageTransfer.becameIdle = { [weak self] id in self?.historySync.resume(id) }
        historySync.shouldPause = { [weak self] id in
            guard let self, self.imageTransfer.deviceID == id else { return false }
            switch self.imageTransfer.phase {
            case .waitingConnection, .preparing, .sending, .displaying: return true
            default: return false
            }
        }
        historySync.saved = { [weak self] inserted in
            guard let self else { return }
            self.resolveLiveClock()
            // Every durable page/clock change reaches the model. Expensive
            // overview and graph reads coalesce independently in the background.
            self.saveRevision += 1
        }
        loadRegisteredDevices()
        central = CBCentralManager(delegate: self, queue: .main, options: [
            CBCentralManagerOptionRestoreIdentifierKey: Self.restorationIdentifier,
            CBCentralManagerOptionShowPowerAlertKey: false,
        ])
    }

    /// Forward scenePhase changes; an inactive/background app never discovery-scans.
    /// Existing connections and pending connection requests remain with iOS.
    public func setForeground(_ foreground: Bool, suspendImageTransfers: Bool = true) {
        isForeground = foreground
        if foreground {
            reconcileConnections()
            scanIfRequested()
            if let id = imageTransfer.deviceID, let target = peripherals[id], clockSync[id] != nil {
                imageTransfer.startQueued(on: target, authenticated: true, foreground: true)
                imageTransfer.resumeDisplayResult(on: target)
            }
        } else {
            // System pairing alerts make the scene inactive while the user is
            // still in this app. Discard a queued photo only on real background.
            if suspendImageTransfers { imageTransfer.foregroundChanged(false) }
            stopScan()
        }
    }

    public func startDiscovery() {
        guard isForeground else {
            lastError = "センサーの登録はアプリを開いて行ってください。"
            return
        }
        discoveryRequested = true
        scanIfRequested()
    }

    public func stopDiscovery() {
        discoveryRequested = false
        stopScan()
    }

    public func startCollection() {
        isCollecting = true
        blockedConnections.removeAll()
        defaults.set(true, forKey: Self.collectionPreference)
        loadRegisteredDevices()
        reconcileConnections()
    }

    public func stopCollection() {
        imageTransfer.cancel()
        isCollecting = false
        defaults.set(false, forKey: Self.collectionPreference)
        stopDiscovery()
        for (id, peripheral) in peripherals {
            resetConnectionData(id)
            connections[id] = .paused
            cancel(peripheral)
        }
    }

    /// The user's explicit registration enables this device and starts collection.
    @discardableResult public func registerDevice(_ device: DiscoveredSensor) -> Bool {
        guard UUID(uuidString: device.id) != nil else {
            lastError = "センサーの識別子が不正です。"
            return false
        }
        do {
            let record = try store.registerDevice(id: device.id, name: device.name,
                                                  enabled: true, seenAt: device.lastSeen)
            registered[record.id] = record
            saveRevision += 1
            startCollection()
            return true
        } catch {
            lastError = "センサーを登録できませんでした: \(error.localizedDescription)"
            return false
        }
    }

    /// Disabling a device preserves its history while cancelling all BLE work for it.
    @discardableResult public func setDeviceEnabled(_ id: String, enabled: Bool) -> Bool {
        do {
            try store.setDeviceEnabled(id: id, enabled: enabled)
            loadRegisteredDevices()
            saveRevision += 1
            if !enabled {
                if imageTransfer.deviceID == id { imageTransfer.cancel() }
                resetConnectionData(id)
                connections[id] = .paused
                if let peripheral = peripherals[id] { cancel(peripheral) }
            } else {
                blockedConnections.remove(id)
                reconcileConnections()
            }
            return true
        } catch {
            lastError = "収集設定を保存できませんでした: \(error.localizedDescription)"
            return false
        }
    }

    /// Call after editing device names/settings through SensorStore elsewhere.
    public func reloadRegisteredDevices() {
        loadRegisteredDevices()
        reconcileConnections()
    }

    public func clearError() { lastError = nil }

    public func sendImage(_ frame: Data, to id: String) {
        guard isForeground, canSendImage(to: id) else {
            lastError = imageSendingUnavailableReason(for: id) ?? "画像送信はアプリを開いて行ってください。"
            return
        }
        imageTransfer.enqueue(frame: frame, deviceID: id)
        startQueuedImageIfReady()
    }

    public func canSendImage(to id: String) -> Bool {
        wantsConnection(id) && !blockedConnections.contains(id) && availability == .ready
    }

    public func imageSendingUnavailableReason(for id: String) -> String? {
        if !isCollecting { return "センサー収集をオンにしてから画像を送信してください。" }
        if registered[id]?.enabled != true { return "このセンサーの収集をオンにしてください。" }
        if blockedConnections.contains(id) { return "ペアリングを確認し、設定から接続を再試行してください。" }
        if availability != .ready { return availability.message }
        return nil
    }

    private func startQueuedImageIfReady() {
        guard isForeground, let id = imageTransfer.deviceID, wantsConnection(id),
              !blockedConnections.contains(id), clockSync[id] != nil,
              !serviceDiscovery.contains(id), !characteristicDiscovery.contains(id), !freshCharacteristics.contains(id),
              let target = peripherals[id] else { return }
        imageTransfer.startQueued(on: target, authenticated: true, foreground: true)
    }

    /// Retry after completing pairing or clearing stale OS/sensor bond information.
    public func retryConnection(_ id: String) {
        guard wantsConnection(id) else { return }
        blockedConnections.remove(id)
        lastError = nil
        rediscoveryAttempted.remove(id)
        dedicatedClockDevices.remove(id)
        clockWriter.removeValue(forKey: id); clockReader.removeValue(forKey: id)
        authenticatedConnections.remove(id)
        if let target = peripherals[id], target.state == .connected {
            requestEnvironmentServices(target)
            return
        }
        reconcileConnections()
    }

    private func loadRegisteredDevices() {
        do {
            registered = Dictionary(uniqueKeysWithValues: try store.devices().map { ($0.id, $0) })
            for device in registered.values where !wantsConnection(device.id) {
                connections[device.id] = .paused
            }
        } catch {
            lastError = "登録したセンサーを読み込めませんでした: \(error.localizedDescription)"
        }
    }

    private func wantsConnection(_ id: String) -> Bool {
        isCollecting && registered[id]?.enabled == true
    }

    private func scanIfRequested() {
        guard discoveryRequested, isForeground, let central, central.state == .poweredOn else { return }
        if !central.isScanning {
            central.scanForPeripherals(withServices: [Self.service], options: [
                CBCentralManagerScanOptionAllowDuplicatesKey: false,
            ])
        }
        isScanning = central.isScanning
    }

    private func stopScan() {
        if let central, central.isScanning { central.stopScan() }
        isScanning = false
    }

    private func reconcileConnections() {
        guard let central, central.state == .poweredOn else { return }
        // Restore objects can arrive before poweredOn. Cancel disallowed objects now.
        for (id, peripheral) in peripherals where !wantsConnection(id) {
            connections[id] = .paused
            cancel(peripheral)
        }
        let missing = registered.values.compactMap { record -> UUID? in
            guard wantsConnection(record.id), peripherals[record.id] == nil else { return nil }
            return UUID(uuidString: record.id)
        }
        for peripheral in central.retrievePeripherals(withIdentifiers: missing) {
            retain(peripheral)
        }
        for record in registered.values where wantsConnection(record.id) {
            guard let peripheral = peripherals[record.id] else {
                connections[record.id] = .failed
                continue
            }
            connectIfNeeded(peripheral)
        }
    }

    private func retain(_ peripheral: CBPeripheral) {
        peripherals[peripheral.identifier.uuidString] = peripheral
        peripheral.delegate = self
    }

    private func connectIfNeeded(_ peripheral: CBPeripheral) {
        let id = peripheral.identifier.uuidString
        guard let central, central.state == .poweredOn, wantsConnection(id) else { return }
        guard !blockedConnections.contains(id) else { connections[id] = .failed; return }
        peripheral.delegate = self
        switch peripheral.state {
        case .disconnected:
            connections[id] = .waiting
            // Core Bluetooth keeps this request pending without a timeout. Retain
            // the object and avoid a background scanner/timer for each minute window.
            central.connect(peripheral, options: [CBConnectPeripheralOptionEnableAutoReconnect: true])
        case .connecting:
            connections[id] = .waiting
        case .connected:
            configure(peripheral)
        case .disconnecting:
            // didDisconnect will recheck the user's current intent.
            break
        @unknown default:
            connections[id] = .failed
        }
    }

    private func cancel(_ peripheral: CBPeripheral) {
        guard let central, central.state == .poweredOn else { return }
        central.cancelPeripheralConnection(peripheral)
    }

    private func resetConnectionData(_ id: String) {
        historySync.disconnected(id)
        capabilitiesDiscovered.remove(id)
        liveClockEligible.remove(id)
        receiver.connectionEnded(for: id)
        characteristics.removeValue(forKey: id)
        initialReads.remove(id)
        pendingReads.remove(id)
        clockWrites.removeValue(forKey: id)
        clockSync.removeValue(forKey: id)
        serviceDiscovery.remove(id)
        characteristicDiscovery.remove(id)
        freshCharacteristics.remove(id)
        rediscoveryAttempted.remove(id)
        dedicatedClockDevices.remove(id)
        clockWriter.removeValue(forKey: id); clockReader.removeValue(forKey: id)
        authenticatedConnections.remove(id)
    }

    private func configure(_ peripheral: CBPeripheral) {
        let id = peripheral.identifier.uuidString
        guard wantsConnection(id), !blockedConnections.contains(id), peripheral.state == .connected else { return }
        // A restored peripheral may expose cached ENV before its history/clock
        // capabilities are discovered for this connection. Do not subscribe yet.
        guard capabilitiesDiscovered.contains(id) else {
            requestEnvironmentServices(peripheral)
            return
        }
        guard let service = peripheral.services?.first(where: { $0.uuid == Self.service }) else {
            requestEnvironmentServices(peripheral)
            return
        }
        if !freshCharacteristics.contains(id), let characteristic = service.characteristics?.first(where: { $0.uuid == Self.characteristic }) {
            subscribe(peripheral, characteristic: characteristic)
        } else {
            guard characteristicDiscovery.insert(id).inserted else { return }
            connections[id] = .subscribing
            peripheral.discoverCharacteristics([Self.characteristic], for: service)
        }
    }

    private func requestEnvironmentServices(_ peripheral: CBPeripheral) {
        let id = peripheral.identifier.uuidString
        guard wantsConnection(id), peripheral.state == .connected,
              serviceDiscovery.insert(id).inserted else { return }
        capabilitiesDiscovered.remove(id)
        freshCharacteristics.insert(id)
        characteristics.removeValue(forKey: id)
        initialReads.remove(id)
        pendingReads.remove(id)
        clockSync.removeValue(forKey: id)
        clockWrites.removeValue(forKey: id)
        connections[id] = .subscribing
        peripheral.discoverServices([Self.service, CardHistorySync.serviceUUID, Self.clockService])
    }

    private func rediscoverIfNeeded(_ error: any Error, operation: String, peripheral: CBPeripheral) -> Bool {
        let id = peripheral.identifier.uuidString
        guard BluetoothFailure(error).canRediscoverServices,
              rediscoveryAttempted.insert(id).inserted else { return false }
        Self.logger.error("\(operation, privacy: .public): ATT rejected cached service; rediscovering once")
        requestEnvironmentServices(peripheral)
        return true
    }

    private func subscribe(_ peripheral: CBPeripheral, characteristic: CBCharacteristic) {
        let id = peripheral.identifier.uuidString
        characteristics[id] = characteristic
        if characteristic.isNotifying {
            connections[id] = .receiving
            readLatest(peripheral, characteristic: characteristic)
        } else if characteristic.properties.contains(.notify) || characteristic.properties.contains(.indicate) {
            connections[id] = .subscribing
            peripheral.setNotifyValue(true, for: characteristic)
        } else {
            connections[id] = .failed
            lastError = "\(deviceName(peripheral)): センサー通知に対応していません。"
            readLatest(peripheral, characteristic: characteristic)
        }
    }

    private func readLatest(_ peripheral: CBPeripheral, characteristic: CBCharacteristic, recovery: Bool = false) {
        let id = peripheral.identifier.uuidString
        guard characteristic.properties.contains(.read), !pendingReads.contains(id) else { return }
        if !recovery, !initialReads.insert(id).inserted { return }
        // A restored already-notifying characteristic may have sent its newest
        // notification while the process was being relaunched. Read fills that gap.
        pendingReads.insert(id)
        peripheral.readValue(for: characteristic)
    }

    private func deviceName(_ peripheral: CBPeripheral) -> String {
        registered[peripheral.identifier.uuidString]?.name ?? peripheral.name ?? "センサー"
    }

    private func synchronizeClock(_ peripheral: CBPeripheral, characteristic: CBCharacteristic) {
        let id = peripheral.identifier.uuidString
        guard authenticatedConnections.contains(id), clockSync[id] == nil else { return }
        let writer: CBCharacteristic
        if dedicatedClockDevices.contains(id) {
            guard let dedicated = clockWriter[id] else { return }
            writer = dedicated
        } else {
            guard characteristic.properties.contains(.write) else { return }
            writer = characteristic
        }
        let now = Date.now
        do {
            let packet = try EnvSensorProtocol.encodeClockSync(
                unixMilliseconds: UInt64(now.timeIntervalSince1970 * 1_000))
            clockWrites[id] = now
            clockSync[id] = .synchronizing
            peripheral.writeValue(packet, for: writer, type: .withResponse)
        } catch {
            clockSync[id] = .failed(error.localizedDescription)
        }
    }

    @discardableResult
    private func report(_ error: any Error, from peripheral: CBPeripheral, operation: String = "接続") -> Bool {
        let id = peripheral.identifier.uuidString
        let failure = BluetoothFailure(error)
        lastError = "\(deviceName(peripheral)): \(failure.message(operation: operation))"
        let native = error as NSError
        Self.logger.error("\(operation, privacy: .public): \(native.domain, privacy: .public) code=\(native.code)")
        connections[id] = .failed
        if failure.requiresUserAction {
            blockedConnections.insert(id)
            imageTransfer.failWaiting(for: id, message: failure.message)
            resetConnectionData(id)
            cancel(peripheral)
        }
        return failure.requiresUserAction
    }

    private func disconnected(_ peripheral: CBPeripheral, systemReconnecting: Bool) {
        let id = peripheral.identifier.uuidString
        imageTransfer.disconnected(id)
        historySync.disconnected(id)
        resetConnectionData(id) // Never combine OF fragments from separate connections.
        startQueuedImageIfReady()
        guard wantsConnection(id) else {
            connections[id] = .paused
            cancel(peripheral) // Also cancels an OS-managed automatic reconnect.
            return
        }
        guard !blockedConnections.contains(id) else {
            connections[id] = .failed
            cancel(peripheral)
            return
        }
        connections[id] = .waiting
        if !systemReconnecting { connectIfNeeded(peripheral) }
    }
}

extension CoreBluetoothCollector: @preconcurrency CBCentralManagerDelegate {
    public func centralManagerDidUpdateState(_ central: CBCentralManager) {
        self.central = central
        availability = switch central.state {
        case .poweredOn: .ready
        case .poweredOff: .poweredOff
        case .unauthorized: .denied
        case .unsupported: .unsupported
        case .resetting: .resetting
        case .unknown: .starting
        @unknown default: .unsupported
        }
        if central.state == .poweredOn {
            blockedConnections.removeAll()
            reconcileConnections()
            scanIfRequested()
        } else {
            imageTransfer.cancel(reason: "Bluetoothが利用できないため画像転送を中止しました。")
            isScanning = false
            for id in peripherals.keys {
                imageTransfer.disconnected(id)
                resetConnectionData(id)
                connections[id] = wantsConnection(id) ? .waiting : .paused
            }
            if central.state == .resetting { peripherals.removeAll() }
        }
    }

    public func centralManager(_ central: CBCentralManager, willRestoreState state: [String: Any]) {
        self.central = central
        // Preserve only registered/enabled collection intent. The user's discovery
        // scan is foreground-only and must not turn into a background scan loop.
        if central.isScanning { central.stopScan() }
        isScanning = false
        let restored = state[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral] ?? []
        for peripheral in restored {
            retain(peripheral)
            let id = peripheral.identifier.uuidString
            resetConnectionData(id)
            if wantsConnection(id) {
                connections[id] = .waiting
                if central.state == .poweredOn, peripheral.state == .connected { requestEnvironmentServices(peripheral) }
            } else {
                connections[id] = .paused
                cancel(peripheral)
            }
        }
    }

    public func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                               advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard isForeground, discoveryRequested else { return }
        retain(peripheral)
        let id = peripheral.identifier.uuidString
        let name = advertisementData[CBAdvertisementDataLocalNameKey] as? String ??
            peripheral.name ?? "環境センサー"
        let candidate = DiscoveredSensor(id: id, name: name, rssi: RSSI.intValue, lastSeen: .now)
        if let index = discoveredDevices.firstIndex(where: { $0.id == id }) {
            discoveredDevices[index] = candidate
        } else {
            discoveredDevices.append(candidate)
        }
        discoveredDevices.sort { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
        if wantsConnection(id) { connectIfNeeded(peripheral) }
    }

    public func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        retain(peripheral)
        let id = peripheral.identifier.uuidString
        guard wantsConnection(id), !blockedConnections.contains(id) else { cancel(peripheral); return }
        resetConnectionData(peripheral.identifier.uuidString)
        requestEnvironmentServices(peripheral)
    }

    public func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                               error: (any Error)?) {
        let id = peripheral.identifier.uuidString
        resetConnectionData(id)
        guard wantsConnection(id) else { connections[id] = .paused; return }
        connections[id] = .failed
        if let error, report(error, from: peripheral) { return }
        // Keep an actual system connection request pending, including while the
        // app is suspended. No minute retry timer or background scan is needed.
        connectIfNeeded(peripheral)
    }

    public func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                               timestamp: CFAbsoluteTime, isReconnecting: Bool, error: (any Error)?) {
        if let error, wantsConnection(peripheral.identifier.uuidString), BluetoothFailure(error).requiresUserAction {
            report(error, from: peripheral)
        }
        disconnected(peripheral, systemReconnecting: isReconnecting)
    }
}

extension CoreBluetoothCollector: @preconcurrency CBPeripheralDelegate {
    public func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: (any Error)?) {
        if imageTransfer.discoveredServices(peripheral, error: error) { return }
        serviceDiscovery.remove(peripheral.identifier.uuidString)
        guard wantsConnection(peripheral.identifier.uuidString),
              !blockedConnections.contains(peripheral.identifier.uuidString) else { return }
        if let error {
            report(error, from: peripheral, operation: "サービスの検索")
        } else if peripheral.services?.contains(where: { $0.uuid == Self.service }) == true {
            let id = peripheral.identifier.uuidString
            historySync.discoveredCapability(peripheral)
            if let service = peripheral.services?.first(where: { $0.uuid == Self.clockService }) {
                dedicatedClockDevices.insert(id)
                peripheral.discoverCharacteristics([Self.clockWrite, Self.clockState], for: service)
            }
            capabilitiesDiscovered.insert(id)
            configure(peripheral)
        } else {
            connections[peripheral.identifier.uuidString] = .failed
            lastError = "\(deviceName(peripheral)): 環境センサーのサービスが見つかりません。"
        }
    }

    public func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                           error: (any Error)?) {
        if service.uuid == Self.clockService {
            let id = peripheral.identifier.uuidString
            if let error { clockSync[id] = .failed("時計サービスの検索: \(error.localizedDescription)") }
            else if let write = service.characteristics?.first(where: { $0.uuid == Self.clockWrite && $0.properties.contains(.write) }),
                    let read = service.characteristics?.first(where: { $0.uuid == Self.clockState && $0.properties.contains(.read) }) {
                clockWriter[id] = write; clockReader[id] = read
                if let environment = characteristics[id] { synchronizeClock(peripheral, characteristic: environment) }
            } else { clockSync[id] = .failed("時計サービスの特性が見つかりません。") }
            return
        }
        if historySync.characteristics(peripheral, service: service, error: error) { return }
        if imageTransfer.discoveredCharacteristics(peripheral, service: service, error: error) { return }
        characteristicDiscovery.remove(peripheral.identifier.uuidString)
        guard wantsConnection(peripheral.identifier.uuidString),
              !blockedConnections.contains(peripheral.identifier.uuidString), service.uuid == Self.service else { return }
        if let error {
            report(error, from: peripheral, operation: "測定データの特性の検索")
        } else if let characteristic = service.characteristics?.first(where: { $0.uuid == Self.characteristic }) {
            freshCharacteristics.remove(peripheral.identifier.uuidString)
            subscribe(peripheral, characteristic: characteristic)
        } else {
            connections[peripheral.identifier.uuidString] = .failed
            lastError = "\(deviceName(peripheral)): 測定データの特性が見つかりません。"
        }
    }

    public func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic,
                           error: (any Error)?) {
        if historySync.notificationState(peripheral, characteristic: characteristic, error: error) { return }
        guard wantsConnection(peripheral.identifier.uuidString),
              !blockedConnections.contains(peripheral.identifier.uuidString),
              characteristic.uuid == Self.characteristic,
              characteristics[peripheral.identifier.uuidString] === characteristic else { return }
        if let error {
            if rediscoverIfNeeded(error, operation: "通知の登録", peripheral: peripheral) { return }
            if report(error, from: peripheral, operation: "通知の登録") { return }
        } else {
            connections[peripheral.identifier.uuidString] = characteristic.isNotifying ? .receiving : .subscribing
        }
        readLatest(peripheral, characteristic: characteristic)
    }

    public func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic,
                           error: (any Error)?) {
        if characteristic.uuid == Self.clockState {
            let id = peripheral.identifier.uuidString
            do {
                if let error { throw error }
                guard let packet = characteristic.value else { throw CardHistoryError.invalidHeader }
                try historySync.clockState(CardDeviceClock(packet: packet), deviceID: id)
            } catch { lastError = "時計の読取: \(error.localizedDescription)" }
            return
        }
        if historySync.received(peripheral, characteristic: characteristic, error: error) { return }
        if imageTransfer.received(peripheral, characteristic: characteristic, error: error) { return }
        let id = peripheral.identifier.uuidString
        let disposition = Self.readingDisposition(capabilitiesDiscovered: capabilitiesDiscovered.contains(id),
                                                  historySupported: historySync.supports(id))
        // Restored subscriptions can deliver an old OE callback before service
        // discovery determines whether this device provides history. Wait for
        // the configured characteristic; its initial Read supplies a complete
        // current value without persisting an extra legacy history row.
        guard disposition != .awaitingCapabilities, wantsConnection(id), !blockedConnections.contains(id), characteristic.uuid == Self.characteristic,
              characteristics[id] === characteristic else { return }
        if let error {
            pendingReads.remove(id)
            if rediscoverIfNeeded(error, operation: "測定値の読取", peripheral: peripheral) { return }
            report(error, from: peripheral, operation: "測定値の読取")
            return
        }
        guard let packet = characteristic.value else { return }
        let isCompleteRead = packet.starts(with: [0x4f, 0x45])
        let previousLive = receiver.liveReadings[id]
        if isCompleteRead { pendingReads.remove(id) }
        do {
            if let saved = try receiver.receive(packet, deviceID: id, persist: disposition == .legacyPersistence) {
                authenticatedConnections.insert(id)
                lastSavedReading = saved
                saveRevision += 1
                connections[id] = .receiving
                synchronizeClock(peripheral, characteristic: characteristic)
            }
            if isCompleteRead || previousLive != receiver.liveReadings[id] {
                authenticatedConnections.insert(id)
                synchronizeClock(peripheral, characteristic: characteristic)
            }
            if historySync.supports(id), isCompleteRead || previousLive != receiver.liveReadings[id],
               let live = receiver.liveReadings[id] {
                liveClockEligible.insert(id)
                liveReadings[id] = live.withMeasuredAt(nil, measurementSource: "card-live-v1")
                resolveLiveClock()
                saveRevision += 1
            }
            if isCompleteRead { synchronizeClock(peripheral, characteristic: characteristic) }
            startQueuedImageIfReady()
            historySync.resume(id)
            if isForeground, clockSync[id] != nil { imageTransfer.resumeDisplayResult(on: peripheral) }
            if isCompleteRead || connections[id] != .recovering {
                connections[id] = .receiving
            }
        } catch let error as SensorProtocolError where error.canRecoverByReadingLatest {
            // Do not save an incomplete notification or invent missing values.
            // A fresh ATT Read recovers the device's complete latest snapshot.
            connections[id] = .recovering
            readLatest(peripheral, characteristic: characteristic, recovery: true)
        } catch {
            lastError = "\(deviceName(peripheral)): \(error.localizedDescription)"
        }
    }

    public func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic,
                           error: (any Error)?) {
        if historySync.wrote(peripheral, characteristic: characteristic, error: error) { return }
        if imageTransfer.wrote(peripheral, characteristic: characteristic, error: error) { return }
        let id = peripheral.identifier.uuidString
        guard characteristic.uuid == Self.characteristic || characteristic.uuid == Self.clockWrite,
              let requestedAt = clockWrites.removeValue(forKey: id) else { return }
        if let error {
            // Older firmware can reject OT without invalidating sensor data.
            clockSync[id] = .failed("時刻を同期できませんでした: \(error.localizedDescription)")
        } else {
            clockSync[id] = .synchronized(requestedAt)
            if let reader = clockReader[id] { peripheral.readValue(for: reader) }
            historySync.clockAcknowledged(id)
        }
    }

    public func peripheral(_ peripheral: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        let id = peripheral.identifier.uuidString
        guard wantsConnection(id) else { return }
        let environmentChanged = invalidatedServices.contains { $0.uuid == Self.service }
        let imageChanged = invalidatedServices.contains { $0.uuid == CBUUID(string: CardImageProtocol.serviceUUID) }
        let historyChanged = invalidatedServices.contains { $0.uuid == CardHistorySync.serviceUUID }
        let clockChanged = invalidatedServices.contains { $0.uuid == Self.clockService }
        guard environmentChanged || imageChanged || historyChanged || clockChanged else { return }
        Self.logger.notice("GATT services invalidated: environment=\(environmentChanged) image=\(imageChanged)")
        if imageChanged { imageTransfer.servicesInvalidated(id) }
        if environmentChanged || historyChanged || clockChanged {
            historySync.disconnected(id)
            resetConnectionData(id)
            requestEnvironmentServices(peripheral)
        } else if isForeground, clockSync[id] != nil {
            imageTransfer.resumeDisplayResult(on: peripheral)
        }
    }

    private func resolveLiveClock() {
        for (id, live) in liveReadings {
            guard liveClockEligible.contains(id), let clock = historySync.clocks[id] else { continue }
            liveReadings[id] = live.withMeasuredAt(clock.date(uptimeMilliseconds: live.uptimeMilliseconds))
        }
    }

    enum ReadingDisposition { case awaitingCapabilities, historyPreview, legacyPersistence }

    nonisolated static func readingDisposition(capabilitiesDiscovered: Bool, historySupported: Bool) -> ReadingDisposition {
        guard capabilitiesDiscovered else { return .awaitingCapabilities }
        return historySupported ? .historyPreview : .legacyPersistence
    }
}
