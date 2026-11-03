import Foundation

struct HistoryPlotTransform {
    enum Phase: Equatable { case began, changed, ended, cancelled }
    let phase: Phase
    let initialCentroidX: Double
    let currentCentroidX: Double
    let plotWidth: Double
    let magnification: Double
}
