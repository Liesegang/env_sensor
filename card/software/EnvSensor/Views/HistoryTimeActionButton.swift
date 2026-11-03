import SwiftUI

struct HistoryTimeActionButton: View {
    let title: String
    let symbol: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Label(title, systemImage: symbol)
                .labelStyle(.iconOnly)
                .frame(minWidth: 44, minHeight: 44)
                .background(.secondary.opacity(0.08), in: .rect(cornerRadius: 10))
        }
        .buttonStyle(.plain)
    }
}
