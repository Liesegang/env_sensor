import Charts
import SwiftUI

/// Only data/range/axis changes invalidate this plot, never the selected time.
struct HistoryOverlayGraph: View, Equatable {
    let metrics: [SensorMetric]
    let series: [SensorMetric: HistoryRenderSeries]
    let scales: [SensorMetric: HistoryValueScale]
    let axes: [SensorMetric]
    let range: ClosedRange<Date>
    // This view deliberately has no cursor DynamicProperty. Only the small
    // interaction layer subscribes to the selection binding.
    let selection: () -> Binding<Date?>
    let onTransform: (HistoryPlotTransform) -> Void
    let isTransformActive: () -> Bool
    let axisWidth: CGFloat
    let headerHeight: CGFloat
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize

    nonisolated static func == (lhs: Self, rhs: Self) -> Bool {
        lhs.metrics == rhs.metrics && lhs.axes == rhs.axes && lhs.range == rhs.range &&
        lhs.axisWidth == rhs.axisWidth && lhs.headerHeight == rhs.headerHeight &&
        lhs.metrics.allSatisfy { lhs.series[$0]?.id == rhs.series[$0]?.id }
    }

    var body: some View {
        Chart { contents }
            .chartXScale(domain: range)
            .chartYScale(domain: 0.0...1.0)
            .chartYAxis(.hidden)
            .chartPlotStyle { $0.clipped() }
            .chartLegend(.hidden)
            .chartXAxis { timeAxis }
            .chartOverlay { proxy in
                ZStack {
                    HistoryOverlayAxesLayer(proxy: proxy, axes: axes, scales: scales,
                                            axisWidth: axisWidth, headerHeight: headerHeight)
                    HistoryChartInteractionLayer(proxy: proxy, range: range, selectedTime: selection(),
                        onTransform: onTransform, isTransformActive: isTransformActive,
                        hasMeasurements: metrics.contains { series[$0]?.points.isEmpty == false })
                }
            }
            .frame(height: dynamicTypeSize.isAccessibilitySize ? 300 : 240)
            .padding(.top, headerHeight + 8)
            .padding(.leading, axes.isEmpty ? 0 : axisWidth + 6)
            .padding(.trailing, CGFloat(max(0, axes.count - 1)) * (axisWidth + 6))
    }

    @ChartContentBuilder private var contents: some ChartContent {
        ForEach(0..<5, id: \.self) { tick in
            RuleMark(y: .value("各軸の目盛り", Double(tick) / 4))
                .foregroundStyle(.secondary.opacity(0.15))
                .lineStyle(StrokeStyle(lineWidth: 0.5))
        }
        ForEach(metrics) { metric in
            if let prepared = series[metric] { HistoryOverlaySeries(metric: metric, series: prepared) }
        }
    }

    @AxisContentBuilder private var timeAxis: some AxisContent {
        AxisMarks(values: .automatic(desiredCount: 3)) { value in
            AxisGridLine().foregroundStyle(.secondary.opacity(0.2))
            AxisValueLabel {
                if let date = value.as(Date.self) {
                    if range.upperBound.timeIntervalSince(range.lowerBound) < 600 {
                        Text(date, format: .dateTime.hour().minute().second())
                    } else if range.upperBound.timeIntervalSince(range.lowerBound) > 86_400 {
                        Text(date, format: .dateTime.month().day())
                    } else {
                        Text(date, format: .dateTime.hour().minute())
                    }
                }
            }
        }
    }
}
