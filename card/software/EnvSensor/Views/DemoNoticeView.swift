import SwiftUI

struct DemoNoticeView: View {
    var body: some View {
        Label("表示テスト用データです。実際の測定値ではありません。", systemImage: "info.circle")
            .font(.footnote).foregroundStyle(.secondary)
    }
}
