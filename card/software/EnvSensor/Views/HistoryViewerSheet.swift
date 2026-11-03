enum HistoryViewerSheet: String, Identifiable {
    case metrics, timeRange, axes
    var id: Self { self }
}
