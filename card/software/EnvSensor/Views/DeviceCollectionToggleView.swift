import SwiftUI

struct DeviceCollectionToggleView: View {
    let model: SensorAppModel
    let device: DeviceRecord
    @State private var enabled = false

    var body: some View {
        VStack(alignment: .leading, spacing: 7) {
            Toggle(device.name, isOn: $enabled)
            Text(model.connectionState(for: device).title)
                .font(.caption).foregroundStyle(.secondary)
            if model.connectionState(for: device) == .failed, let collector = model.collector {
                Button("接続を再試行", systemImage: "arrow.clockwise") {
                    collector.retryConnection(device.id)
                }
            }
        }
        .onChange(of: device.enabled, initial: true) { enabled = device.enabled }
        .onChange(of: enabled, enabledChanged)
    }
    private func enabledChanged(_ oldValue: Bool, _ newValue: Bool) {
        guard newValue != device.enabled else { return }
        if !model.setEnabled(device, newValue) { enabled = oldValue }
    }
}
