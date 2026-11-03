import SwiftUI

struct DashboardView: View {
    @Bindable var model: SensorAppModel
    @State private var showingDiscovery = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 24) {
                    CollectionSummaryView(model: model)
                    if model.devices.isEmpty {
                        ContentUnavailableView {
                            Label("センサーを登録", systemImage: "sensor")
                        } description: {
                            Text("card・outdoor・roomから測定値を受け取り、iPhoneに蓄積します。")
                        } actions: {
                            Button("デバイスを追加", systemImage: "plus", action: openDiscovery)
                                .buttonStyle(.borderedProminent)
                        }
                    } else {
                        ForEach(model.devices) { device in
                            DeviceCardView(model: model, device: device)
                        }
                    }
                    Text("受信した値は、このiPhoneに保存されます。")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                .padding()
            }
            .background(.background)
            .navigationTitle("環境センサー")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("デバイスを追加", systemImage: "plus", action: openDiscovery)
                        .disabled(model.isDemo)
                }
            }
            .navigationDestination(for: String.self) { id in
                DeviceHistoryView(model: model, deviceID: id)
            }
            .sheet(isPresented: $showingDiscovery) {
                DiscoveryView(model: model)
            }
            .refreshable { model.refresh() }
        }
    }
    private func openDiscovery() { showingDiscovery = true }
}
