import Foundation

public enum CardImageError: Error, LocalizedError, Sendable, Equatable {
    case invalidFrame
    case invalidPalette
    case invalidTransfer
    case invalidStatus
    case unsupported
    case notConnected
    case timedOut
    case device(Int32)
    case decoding
    case cancelled
    case operation(String, String)

    public var errorDescription: String? {
        switch self {
        case .invalidFrame: "画像データの大きさが不正です。"
        case .invalidPalette: "画像に未対応の色が含まれています。"
        case .invalidTransfer: "画像転送の設定が不正です。"
        case .invalidStatus: "cardから届いた画像転送の状態が不正です。"
        case .unsupported: "このデバイスは画像転送に対応していません。対応するcardファームウェアが必要です。"
        case .notConnected: "cardに接続してから画像を送信してください。"
        case .timedOut: "画像転送の応答がありません。cardの接続を確認してください。"
        case .device(let code): "cardで画像を処理できませんでした（エラー \(code)）。以前の画像は保持されます。"
        case .decoding: "この写真を読み込めませんでした。別の画像を選んでください。"
        case .cancelled: "画像の転送を中止しました。"
        case .operation(let operation, let reason): "\(operation): \(reason)"
        }
    }
}
