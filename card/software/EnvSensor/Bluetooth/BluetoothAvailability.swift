import Foundation

public enum BluetoothAvailability: String, Sendable {
    case starting
    case ready
    case poweredOff
    case denied
    case unsupported
    case resetting

    public var title: String {
        switch self {
        case .starting: "Bluetoothを準備中"
        case .ready: "Bluetooth使用可能"
        case .poweredOff: "Bluetoothがオフ"
        case .denied: "Bluetoothの許可が必要"
        case .unsupported: "Bluetooth収集に未対応"
        case .resetting: "Bluetoothを再準備中"
        }
    }

    public var message: String {
        switch self {
        case .starting: "Bluetoothの許可と状態を確認しています。"
        case .ready: "登録したセンサーの次の通信窓を待ちます。"
        case .poweredOff: "Bluetoothをオンにすると、収集を再開します。"
        case .denied: "設定アプリで、このアプリのBluetooth使用を許可してください。"
        case .unsupported: "この端末ではBluetoothによるセンサー収集を利用できません。"
        case .resetting: "Bluetoothの復旧後に、登録したセンサーへ再接続します。"
        }
    }
}
