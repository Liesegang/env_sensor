import SwiftUI

struct HistoryOverlayAxis: View {
    let metric: SensorMetric
    let scale: HistoryValueScale?
    let height: CGFloat
    let width: CGFloat
    let headerHeight: CGFloat

    var body: some View {
        ZStack {
            ForEach(0..<5) { index in
                let fraction = Double(index) / 4
                Text(metric.axisFormatted(scale?.actualValue(at: fraction)))
                    .font(.footnote.monospacedDigit())
                    .lineLimit(1)
                    .frame(width: width)
                    .position(x: width / 2, y: height * CGFloat(1 - fraction))
            }
        }
        .frame(width: width, height: height)
        .foregroundStyle(metric.color)
        .overlay(alignment: .top) {
            VStack(spacing: 2) {
                Text(metric.axisTitle).bold()
                Text(metric.unit)
            }
            .font(.footnote).lineLimit(1)
            .frame(width: width)
            .offset(y: -headerHeight)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("\(metric.title)の軸、\(metric.unit)")
        .accessibilityValue(scale.map { "\(metric.formatted($0.lowerBound))から\(metric.formatted($0.upperBound))" } ?? "記録なし")
    }
}
