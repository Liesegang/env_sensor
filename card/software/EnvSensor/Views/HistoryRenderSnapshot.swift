import Foundation

struct HistoryRenderSnapshot: Sendable {
    let viewport: HistoryViewport
    let readings: [StoredReading]
    let series: [SensorMetric: HistoryRenderSeries]
    let scales: [SensorMetric: HistoryValueScale]
    let nominalInterval: TimeInterval
    let selection: HistorySelectionIndex
    let fixedScales: [SensorMetric: HistoryValueScale]?

    static func prepare(readings: [StoredReading], viewport: HistoryViewport,
                        fixedScales requestedFixedScales: [SensorMetric: HistoryValueScale]? = nil,
                        previous: Self? = nil) throws -> Self {
        try Task.checkCancellation()
        if let previous, previous.viewport == viewport, previous.readings == readings,
           previous.fixedScales == requestedFixedScales { return previous }
        var fixedScales = requestedFixedScales
        var series: [SensorMetric: HistoryRenderSeries] = [:]
        for metric in SensorMetric.allCases {
            try Task.checkCancellation()
            let points = HistoryChartPoint.make(readings: readings, metric: metric, since: viewport.start,
                                                maximumGapSeconds: viewport.maximumGapSeconds)
            let prepared = HistoryRenderSeries(key: metric.rawValue, samples: points.map {
                HistoryRenderSample(timestamp: $0.timestamp, value: $0.value,
                    segment: $0.segment, isIsolated: $0.isIsolated)
            }, minimumSpan: metric.minimumPlotSpan, forcedScale: fixedScales?[metric])
            series[metric] = prepared
            // A sensor without measurements has no invented scale. Its first
            // real scale is captured once if it becomes available while fixed.
            if fixedScales != nil, fixedScales?[metric] == nil, let scale = prepared.scale {
                fixedScales?[metric] = scale
            }
        }
        try Task.checkCancellation()
        return Self(viewport: viewport, readings: readings, series: series, scales: series.compactMapValues(\.scale),
                    nominalInterval: HistorySelection.nominalInterval(in: readings),
                    selection: HistorySelectionIndex(readings: readings), fixedScales: fixedScales)
    }
}
