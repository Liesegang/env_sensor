import Charts
import SwiftUI

struct HistorySeparatePlot: View, Equatable {
    let series: HistoryRenderSeries
    let metric: SensorMetric
    let range: ClosedRange<Date>
    let selection: () -> Binding<Date?>
    let onTransform: (HistoryPlotTransform) -> Void
    let isTransformActive: () -> Bool
    let axisLabelWidth: CGFloat
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize

    nonisolated static func == (lhs: Self, rhs: Self) -> Bool {
        lhs.series.id == rhs.series.id && lhs.metric == rhs.metric && lhs.range == rhs.range &&
        lhs.axisLabelWidth == rhs.axisLabelWidth
    }

    var body: some View {
        Chart {
            LinePlot(series.points, x: .value("測定時刻", \.timestamp),
                     y: .value(metric.title, \.actualValue), series: .value("測定の区間", \.seriesID))
                .foregroundStyle(metric.color)
                .lineStyle(StrokeStyle(lineWidth: 2))
                .interpolationMethod(.linear)
            if !series.isolatedPoints.isEmpty {
                PointPlot(series.isolatedPoints, x: .value("測定時刻", \.timestamp),
                          y: .value(metric.title, \.actualValue))
                    .foregroundStyle(metric.color)
            }
        }
        .chartXScale(domain: range)
        .chartYScale(domain: (series.scale?.lowerBound ?? 0)...(series.scale?.upperBound ?? 1))
        .chartPlotStyle { $0.clipped() }
        .chartXAxis { timeAxis }
        .chartYAxis {
            AxisMarks(position: .leading, values: .automatic(desiredCount: 3)) { value in
                AxisGridLine().foregroundStyle(.secondary.opacity(0.2))
                AxisValueLabel {
                    if let number = value.as(Double.self) {
                        Text(series.scale == nil ? "—" : number.formatted(.number.precision(.significantDigits(3))))
                            .frame(width: axisLabelWidth, alignment: .trailing)
                    }
                }
            }
        }
        .chartOverlay {
            HistoryChartInteractionLayer(proxy: $0, range: range, selectedTime: selection(),
                onTransform: onTransform, isTransformActive: isTransformActive,
                hasMeasurements: !series.points.isEmpty)
        }
        .chartLegend(.hidden)
        .frame(height: dynamicTypeSize.isAccessibilitySize ? 240 : 180)
    }

    @AxisContentBuilder private var timeAxis: some AxisContent {
        AxisMarks(values: .automatic(desiredCount: dynamicTypeSize.isAccessibilitySize ? 2 : 4)) { value in
            AxisGridLine().foregroundStyle(.secondary.opacity(0.2))
            AxisValueLabel {
                if let date = value.as(Date.self) {
                    if range.upperBound.timeIntervalSince(range.lowerBound) < 600 {
                        Text(date, format: .dateTime.hour().minute().second())
                    } else if range.upperBound.timeIntervalSince(range.lowerBound) > 86_400 {
                        Text(date, format: .dateTime.month().day().hour())
                    } else {
                        Text(date, format: .dateTime.hour().minute())
                    }
                }
            }
        }
    }
}
