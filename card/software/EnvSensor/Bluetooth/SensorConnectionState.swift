import Foundation

public enum SensorConnectionState: String, Sendable {
    case waiting
    case connected
    case subscribing
    case receiving
    case recovering
    case paused
    case failed

    public var title: String {
        switch self {
        case .waiting: "通信窓を待機中"
        case .connected: "接続済み"
        case .subscribing: "通知を準備中"
        case .receiving: "受信中"
        case .recovering: "最新値を取得し直しています"
        case .paused: "収集を停止中"
        case .failed: "接続を確認してください"
        }
    }
}
