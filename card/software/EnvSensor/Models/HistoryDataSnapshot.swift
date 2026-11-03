import Foundation

public struct HistoryDataSnapshot: Sendable {
    public let readings: [StoredReading]
    public let availableRange: ClosedRange<Date>?
    public let timeSummary: ReadingTimeSummary
}

public struct SensorOverviewSnapshot: Sendable {
    public let devices: [DeviceRecord]
    public let latest: [StoredReading]
    public let recent: [StoredReading]
    public let counts: StoreCounts
}
