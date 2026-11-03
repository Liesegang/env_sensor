import SwiftUI

@main
struct EnvSensorApp: App {
    @State private var model: SensorAppModel?
    @State private var startupError: String?

    init() {
        do {
            _model = State(initialValue: try SensorAppModel.open())
        } catch {
            _startupError = State(initialValue: error.localizedDescription)
        }
    }

    var body: some Scene {
        WindowGroup {
            if let model {
                RootTabView(model: model)
            } else {
                ContentUnavailableView {
                    Label("保存データを開けません", systemImage: "externaldrive.badge.exclamationmark")
                } description: {
                    Text(startupError ?? "iPhoneをロック解除してから、もう一度お試しください。")
                } actions: {
                    Button("もう一度開く", action: retry)
                }
            }
        }
    }

    private func retry() {
        do {
            model = try SensorAppModel.open()
            startupError = nil
        } catch {
            startupError = error.localizedDescription
        }
    }
}
