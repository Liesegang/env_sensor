import Charts
import SwiftUI

struct HistoryOverlaySeries: ChartContent {
    let metric: SensorMetric
    let series: HistoryRenderSeries

    var body: some ChartContent {
        LinePlot(series.points, x: .value("測定時刻", \.timestamp),
                 y: .value(metric.title, \.normalizedValue), series: .value("項目と測定区間", \.seriesID))
            .foregroundStyle(metric.color)
            .lineStyle(StrokeStyle(lineWidth: 2, dash: metric.plotDash))
            .interpolationMethod(.linear)
        if !series.isolatedPoints.isEmpty {
            PointPlot(series.isolatedPoints, x: .value("測定時刻", \.timestamp),
                      y: .value(metric.title, \.normalizedValue))
                .foregroundStyle(metric.color)
        }
    }
}
