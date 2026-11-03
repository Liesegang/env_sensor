import SwiftUI

struct DiscoveryDeviceRow: View {
    let device: DiscoveredSensor
    let registered: Bool
    let register: () -> Void

    var body: some View {
        HStack(spacing: 16) {
            Image(systemName: "sensor").foregroundStyle(Color.accentColor).accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 5) {
                Text(device.name).font(.headline)
                Text("信号 \(device.rssi) dBm").font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            if registered {
                Label("登録済み", systemImage: "checkmark.circle.fill")
                    .font(.caption).foregroundStyle(Color.accentColor)
            } else {
                Button("登録", action: register).buttonStyle(.bordered)
                    .accessibilityLabel("\(device.name)を登録")
            }
        }
        .padding(.vertical, 6)
    }
}
