import Foundation

/// A foreground discovery candidate. Discovery alone does not enable collection.
public struct DiscoveredSensor: Identifiable, Sendable, Equatable {
    public let id: String
    public let name: String
    public let rssi: Int
    public let lastSeen: Date

    public init(id: String, name: String, rssi: Int, lastSeen: Date) {
        self.id = id
        self.name = name
        self.rssi = rssi
        self.lastSeen = lastSeen
    }
}
