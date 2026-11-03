import Foundation

public enum SensorClockSyncState: Equatable, Sendable {
    case synchronizing
    case synchronized(Date)
    case failed(String)

    public var title: String {
        switch self {
        case .synchronizing: "本体の時刻を同期中"
        case .synchronized: "本体の時刻を同期済み"
        case .failed(let message): message
        }
    }
}
