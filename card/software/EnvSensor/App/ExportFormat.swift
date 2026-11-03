enum ExportFormat: String, CaseIterable, Identifiable, Sendable {
    case json
    case csv
    var id: Self { self }
    var title: String { rawValue.uppercased() }
}
