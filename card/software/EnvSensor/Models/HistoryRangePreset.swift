import Foundation

public enum HistoryRangePreset: String, CaseIterable, Identifiable, Sendable {
    case hour, sixHours, day, week
    public var id: Self { self }
    public var title: String {
        switch self {
        case .hour: "1時間"
        case .sixHours: "6時間"
        case .day: "24時間"
        case .week: "7日"
        }
    }
    public var duration: TimeInterval {
        switch self {
        case .hour: 3_600
        case .sixHours: 21_600
        case .day: 86_400
        case .week: 604_800
        }
    }
}
