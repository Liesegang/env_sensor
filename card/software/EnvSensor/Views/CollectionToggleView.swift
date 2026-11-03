import SwiftUI

struct CollectionToggleView: View {
    let model: SensorAppModel
    @State private var collecting = true

    var body: some View {
        Toggle("測定値を受信・保存", isOn: $collecting)
            .disabled(model.isDemo)
            .onChange(of: model.collector?.isCollecting, initial: true) {
                collecting = model.collector?.isCollecting ?? true
            }
            .onChange(of: collecting, collectionChanged)
    }
    private func collectionChanged(_ oldValue: Bool, _ newValue: Bool) {
        guard let collector = model.collector, collector.isCollecting != newValue else { return }
        if newValue { collector.startCollection() } else { collector.stopCollection() }
    }
}
