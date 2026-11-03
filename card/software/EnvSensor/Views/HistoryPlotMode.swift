enum HistoryPlotMode: String, CaseIterable, Identifiable {
    case overlay, separate
    var id: Self { self }
    var title: String { self == .overlay ? "重ねて表示" : "項目別に表示" }
}
