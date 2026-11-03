import Foundation

/// Immutable identity lets SwiftUI keep the expensive plot while its cursor moves.
public final class HistoryRenderSeries: Sendable {
    public let id = UUID()
    public let points: [HistoryRenderPoint]
    public let isolatedPoints: [HistoryRenderPoint]
    public let scale: HistoryValueScale?

    public init(key: String, samples: [HistoryRenderSample], minimumSpan: Double, forcedScale: HistoryValueScale? = nil) {
        let valid = samples.filter { $0.value.isFinite && $0.timestamp.timeIntervalSince1970.isFinite }
        scale = forcedScale ?? HistoryValueScale(values: valid.map(\.value), minimumSpan: minimumSpan)
        guard let scale else { points = []; isolatedPoints = []; return }
        var prepared: [HistoryRenderPoint] = []
        var isolated: [HistoryRenderPoint] = []
        prepared.reserveCapacity(valid.count)
        for sample in valid {
            guard let normalized = scale.normalized(sample.value) else { continue }
            let point = HistoryRenderPoint(timestamp: sample.timestamp, actualValue: sample.value,
                normalizedValue: normalized, seriesID: "\(key)-\(sample.segment)")
            prepared.append(point)
            if sample.isIsolated { isolated.append(point) }
        }
        points = prepared
        isolatedPoints = isolated
    }
}
