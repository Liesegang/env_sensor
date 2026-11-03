import Foundation

/// Serialized by the collector's MainActor. Kept independent of Core Bluetooth
/// so Read/Notify ordering and committed persistence can be checked offline.
@MainActor
final class SensorPacketReceiver {
    private let store: SensorStore
    private var assemblers: [String: FragmentAssembler] = [:]
    private var acceptedUptime: [String: UInt64] = [:]
    private(set) var liveReadings: [String: SensorReading] = [:]

    init(store: SensorStore) { self.store = store }

    func connectionEnded(for deviceID: String) {
        assemblers[deviceID]?.reset()
        assemblers.removeValue(forKey: deviceID)
        acceptedUptime.removeValue(forKey: deviceID)
    }

    /// nil means a partial fragment, a persisted duplicate, or an older Read
    /// that completed after a newer Notify on this same connection.
    func receive(_ packet: Data, deviceID: String, receivedAt: Date = .now, persist: Bool = true) throws -> StoredReading? {
        let reading: SensorReading?
        if packet.starts(with: [0x4f, 0x46]) {
            let assembler = assemblers[deviceID] ?? FragmentAssembler()
            assemblers[deviceID] = assembler
            reading = try assembler.push(packet, receivedAt: receivedAt)
        } else {
            // A complete Read and an OF Notify stream may legitimately interleave.
            // Reading must not discard that stream's partial assembly.
            reading = try EnvSensorProtocol.decode(packet, receivedAt: receivedAt)
        }
        guard let reading else { return nil }
        if let latest = acceptedUptime[deviceID], reading.uptimeMilliseconds < latest {
            return nil
        }
        let saved = try persist ? store.save(reading, deviceID: deviceID) : nil
        // A real MCU reboot disconnects BLE. Resetting this connection-local
        // guard then lets SensorStore detect the lower uptime on reconnection.
        acceptedUptime[deviceID] = reading.uptimeMilliseconds
        liveReadings[deviceID] = reading
        let assembler = assemblers[deviceID] ?? FragmentAssembler()
        assemblers[deviceID] = assembler
        assembler.rememberCompleted(reading)
        return saved
    }
}
