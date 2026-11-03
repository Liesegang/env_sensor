import Foundation

public enum CardImageTransferPhase: Sendable, Equatable {
    case idle
    case waitingConnection
    case preparing
    case sending(Double)
    case displaying
    case waitingForDisplay
    case complete
    case failed(String)
    case cancelled(String)

    public var isBusy: Bool {
        switch self {
        case .waitingConnection, .preparing, .sending, .displaying, .waitingForDisplay: true
        default: false
        }
    }

    public var title: String {
        switch self {
        case .idle: "送信する写真を選んでください"
        case .waitingConnection: "cardの通信を待っています（最大約1分）"
        case .preparing: "cardの保存先を準備中"
        case .sending(let fraction): "画像を送信中 \(Int(fraction * 100))%"
        case .displaying: "cardの画面を更新中"
        case .waitingForDisplay: "cardで表示処理を続けています。接続後に結果を確認します。"
        case .complete: "cardに画像を表示しました"
        case .failed(let message), .cancelled(let message): message
        }
    }
}
