import Foundation
import Observation

@MainActor
@Observable
final class SensorAppModel {
    let collector: CoreBluetoothCollector?
    let isDemo: Bool
    private(set) var devices: [DeviceRecord] = []
    private(set) var latest: [StoredReading] = []
    private(set) var recent: [StoredReading] = []
    private(set) var counts: StoreCounts?
    private(set) var historyRevision = 0
    var errorMessage: String?
    @ObservationIgnored private let store: SensorStore
    @ObservationIgnored private var refreshTask: Task<Void, Never>?
    @ObservationIgnored private var refreshPending = false
    @ObservationIgnored private var historyRequests: [String: UUID] = [:]
    @ObservationIgnored private var deviceSettingsRevision = 0

    init(store: SensorStore, isDemo: Bool = false) {
        self.store = store
        self.isDemo = isDemo
        collector = isDemo ? nil : CoreBluetoothCollector(store: store)
        refresh()
    }

    static func open() throws -> SensorAppModel {
        #if DEBUG
        if ProcessInfo.processInfo.arguments.contains("--ui-demo") {
            let directory = FileManager.default.temporaryDirectory.appendingPathComponent("EnvSensorDemo", isDirectory: true)
            let store = try SensorStore(url: directory.appendingPathComponent("demo.sqlite"))
            try DemoReadings.populate(store)
            return SensorAppModel(store: store, isDemo: true)
        }
        #endif
        return SensorAppModel(store: try SensorStore(url: SensorStore.defaultURL()))
    }

    func refresh() {
        refreshPending = true
        guard refreshTask == nil else { return }
        refreshTask = Task { [weak self] in
            guard let self else { return }
            repeat {
                self.refreshPending = false
                let settingsRevision = self.deviceSettingsRevision
                do {
                    let snapshot = try await self.store.loadOverview()
                    guard !Task.isCancelled else { break }
                    if settingsRevision == self.deviceSettingsRevision { self.devices = snapshot.devices }
                    self.latest = snapshot.latest
                    self.recent = snapshot.recent
                    self.counts = snapshot.counts
                    // Old backlog and clock corrections can leave recent200
                    // unchanged. Publish a separate revision for all history.
                    self.historyRevision += 1
                } catch is CancellationError { break }
                catch { self.errorMessage = "保存データを読み込めませんでした: \(error.localizedDescription)" }
            } while self.refreshPending && !Task.isCancelled
            self.refreshTask = nil
        }
    }

    func loadHistorySnapshot(deviceID: String, since: Date, until: Date, bucketSeconds: Int) async throws -> HistoryDataSnapshot {
        let request = UUID()
        historyRequests[deviceID] = request
        let snapshot = try await store.loadHistorySnapshot(deviceID: deviceID, since: since, until: until, bucketSeconds: bucketSeconds)
        guard !Task.isCancelled, historyRequests[deviceID] == request else { throw CancellationError() }
        return snapshot
    }

    func loadMeasurementRange(deviceID: String) async throws -> ClosedRange<Date>? {
        try await store.loadMeasurementRange(deviceID: deviceID)
    }

    func deviceName(_ id: String) -> String {
        devices.first { $0.id == id }?.name ?? "環境センサー"
    }

    func latestReading(for id: String) -> StoredReading? {
        if let live = collector?.liveReadings[id] {
            return StoredReading(id: -1, deviceID: id, bootID: "live-preview", reading: live)
        }
        return latest.first { $0.deviceID == id }
    }

    func connectionState(for device: DeviceRecord) -> SensorConnectionState {
        if !device.enabled || collector?.isCollecting == false { return .paused }
        return collector?.connections[device.id] ?? .waiting
    }

    @discardableResult
    func setEnabled(_ device: DeviceRecord, _ enabled: Bool) -> Bool {
        if let collector {
            guard collector.setDeviceEnabled(device.id, enabled: enabled) else { return false }
        } else if isDemo {
            do { try store.setDeviceEnabled(id: device.id, enabled: enabled) }
            catch {
                errorMessage = error.localizedDescription
                return false
            }
        }
        deviceSettingsRevision += 1
        devices = devices.map { record in
            record.id == device.id ? DeviceRecord(id: record.id, name: record.name, enabled: enabled,
                firstSeen: record.firstSeen, lastSeen: record.lastSeen) : record
        }
        refresh()
        return true
    }

    @discardableResult func registerDevice(_ device: DiscoveredSensor) -> Bool {
        guard collector?.registerDevice(device) == true else { return false }
        deviceSettingsRevision += 1
        if let index = devices.firstIndex(where: { $0.id == device.id }) {
            let previous = devices[index]
            devices[index] = DeviceRecord(id: device.id, name: device.name, enabled: true,
                firstSeen: previous.firstSeen, lastSeen: max(previous.lastSeen, device.lastSeen))
        } else {
            devices.append(DeviceRecord(id: device.id, name: device.name, enabled: true,
                firstSeen: device.lastSeen, lastSeen: device.lastSeen))
        }
        refresh()
        return true
    }

    func prepareExport(format: ExportFormat, deviceID: String? = nil, since: Date? = nil, until: Date? = nil) async -> URL? {
        let exportStore = store
        do {
            let data = try await format == .json ? exportStore.loadExportJSON(deviceID: deviceID, since: since, until: until)
                : exportStore.loadExportCSV(deviceID: deviceID, since: since, until: until)
            let writer = Task.detached(priority: .userInitiated) {
                try Task.checkCancellation()
                let directory = FileManager.default.temporaryDirectory.appendingPathComponent("EnvSensorExports", isDirectory: true)
                try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                let timestamp = Int(Date().timeIntervalSince1970)
                let url = directory.appendingPathComponent("env-sensor-\(timestamp)-\(UUID().uuidString.prefix(8)).\(format.rawValue)")
                try data.write(to: url, options: .atomic)
                if Task.isCancelled { try? FileManager.default.removeItem(at: url); throw CancellationError() }
                return url
            }
            return try await withTaskCancellationHandler { try await writer.value } onCancel: { writer.cancel() }
        } catch is CancellationError { return nil }
        catch {
            errorMessage = "書き出しに失敗しました: \(error.localizedDescription)"
            return nil
        }
    }
}
