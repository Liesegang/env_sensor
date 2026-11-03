import SwiftUI

struct RootTabView: View {
    @Bindable var model: SensorAppModel
    @Environment(\.scenePhase) private var scenePhase
    @State private var selection = AppTab.sensors

    var body: some View {
        TabView(selection: $selection) {
            Tab("センサー", systemImage: "sensor", value: .sensors) {
                DashboardView(model: model)
            }
            Tab("履歴", systemImage: "chart.xyaxis.line", value: .history) {
                HistoryView(model: model)
            }
            Tab("設定", systemImage: "gearshape", value: .settings) {
                SettingsView(model: model)
            }
        }
        .onChange(of: scenePhase, initial: true, sceneChanged)
        .onChange(of: model.collector?.saveRevision) { model.refresh() }
        .alert("データの確認が必要です", isPresented: errorPresented) {
            Button("閉じる", role: .cancel, action: clearError)
        } message: {
            Text(model.errorMessage ?? model.collector?.lastError ?? "")
        }
    }

    private var errorPresented: Binding<Bool> {
        Binding(get: { model.errorMessage != nil || model.collector?.lastError != nil },
                set: { if !$0 { clearError() } })
    }
    private func clearError() {
        model.errorMessage = nil
        model.collector?.clearError()
    }
    private func sceneChanged(_ oldValue: ScenePhase, _ newValue: ScenePhase) {
        model.collector?.setForeground(newValue == .active, suspendImageTransfers: newValue == .background)
        if newValue == .active { model.refresh() }
    }
}
