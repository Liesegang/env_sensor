import CoreBluetooth
import Foundation

/// Core Bluetooth owns the keys and presents the standard passkey dialog.
/// These failures require user action rather than another immediate connect.
struct BluetoothFailure {
    let message: String
    let requiresUserAction: Bool
    let canRediscoverServices: Bool

    init(_ error: any Error) {
        let error = error as NSError
        var explanation: String?
        var canRediscover = false
        var userAction = false
        if error.domain == CBErrorDomain {
            switch CBError.Code(rawValue: error.code) {
            case .peerRemovedPairingInformation:
                explanation = "ペアリング情報が一致していません。iPhoneのBluetooth設定にこのセンサーがあれば登録を解除し、センサー側のペアリング情報も削除してから、もう一度接続してください。"
            case .encryptionTimedOut:
                explanation = "ペアリングが時間内に完了しませんでした。アプリを開いたまま再接続し、iPhoneのペアリング画面でセンサーに表示された6桁のコードを入力してください。"
            case .tooManyLEPairedDevices:
                explanation = "iPhoneに登録できるBluetooth機器の上限に達しました。不要な機器のペアリングを解除して、もう一度接続してください。"
            case .connectionLimitReached:
                explanation = "Bluetoothの同時接続数が上限に達しました。他の機器との接続を減らして、もう一度接続してください。"
            default:
                break
            }
        } else if error.domain == CBATTErrorDomain {
            switch CBATTError.Code(rawValue: error.code) {
            case .writeNotPermitted:
                explanation = "デバイスが書き込みを許可していません。ファームウェア更新後なら、サービス情報を取得し直してください。"
                canRediscover = true
            case .readNotPermitted, .invalidHandle, .attributeNotFound:
                explanation = "デバイスのサービス情報を確認できません。ファームウェア更新後なら、接続を再試行してください。"
                canRediscover = true
            case .insufficientAuthentication, .insufficientAuthorization,
                 .insufficientEncryption, .insufficientEncryptionKeySize:
                explanation = "センサーとの認証が完了していません。アプリを開いたまま再接続し、iPhoneのペアリング画面でセンサーに表示された6桁のコードを入力してください。"
                userAction = true
            default:
                break
            }
        }
        requiresUserAction = userAction || (error.domain == CBErrorDomain && explanation != nil)
        canRediscoverServices = canRediscover
        message = explanation ?? error.localizedDescription
    }

    func message(operation: String) -> String { "\(operation): \(message)" }
}
