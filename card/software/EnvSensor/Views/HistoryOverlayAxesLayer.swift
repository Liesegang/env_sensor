import Charts
import SwiftUI

struct HistoryOverlayAxesLayer: View {
    let proxy: ChartProxy
    let axes: [SensorMetric]
    let scales: [SensorMetric: HistoryValueScale]
    let axisWidth: CGFloat
    let headerHeight: CGFloat

    var body: some View {
        GeometryReader { geometry in
            if let anchor = proxy.plotFrame {
                let rect = geometry[anchor]
                ForEach(axes.indices, id: \.self) { index in
                    let metric = axes[index]
                    HistoryOverlayAxis(metric: metric, scale: scales[metric],
                        height: rect.height, width: axisWidth, headerHeight: headerHeight)
                        .position(x: xPosition(index: index, plot: rect), y: rect.midY)
                }
            }
        }
        .allowsHitTesting(false)
    }

    private func xPosition(index: Int, plot: CGRect) -> CGFloat {
        if index == 0 { return plot.minX - axisWidth / 2 - 6 }
        return plot.maxX + axisWidth / 2 + 6 + CGFloat(index - 1) * (axisWidth + 6)
    }
}
