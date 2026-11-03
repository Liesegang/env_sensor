import SwiftUI

struct DeviceHistoryView: View {
    @Bindable var model: SensorAppModel
    let deviceID: String
    @State private var selectedMetrics = Set(SensorMetric.allCases)
    @State private var plotMode = HistoryPlotMode.overlay
    @State private var selectedAxes: Set<SensorMetric> = [.temperature, .humidity, .co2]
    @State private var viewport = HistoryViewport(endingAt: .now, duration: 86_400)
    @State private var available: ClosedRange<Date>?
    @State private var rendered: HistoryRenderSnapshot?
    @State private var summary: ReadingTimeSummary?
    @State private var selectedTime: Date?
    @State private var selectedSample: StoredReading?
    @State private var sheet: HistoryViewerSheet?
    @State private var reloadRevision = 0
    @State private var initialized = false
    @State private var followsLatest = true
    @State private var isLoading = false
    @State private var needsReload = false
    @State private var loadError: String?
    @State private var automaticReload: Task<Void, Never>?
    @State private var lastReloadStarted: ContinuousClock.Instant?
    @State private var transformStart: HistoryViewport?
    @State private var transformBounds: ClosedRange<Date>?
    @State private var previewViewport: HistoryViewport?
    @State private var isYAxisFixed = false
    @State private var fixedScales: [SensorMetric: HistoryValueScale] = [:]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                if model.isDemo { DemoNoticeView() }
                HistoryTimeControls(viewport: viewport,
                    canMoveEarlier: viewport.moved(fraction: -0.5, within: movementBounds) != viewport,
                    canMoveLater: viewport.moved(fraction: 0.5, within: movementBounds) != viewport,
                    onPreset: applyPreset, onCustom: { sheet = .timeRange },
                    onMove: move, onZoom: zoom, onLatest: returnToLatest)
                HistoryValueCards(metrics: metrics, sample: selectedSample ?? (selectedTime == nil ? readings.last : nil),
                                  selectedTime: selectedTime, onClear: { selectedTime = nil })
                if let summary {
                    VStack(alignment: .leading, spacing: 6) {
                        Text("この期間：\(summary.measuredCount + summary.legacyReceivedCount)件保存")
                            .font(.subheadline).foregroundStyle(.secondary)
                        if summary.unknownTimeCount > 0 {
                            Label("日時未確定：\(summary.unknownTimeCount)件（全期間）。グラフには含めません。", systemImage: "clock.badge.questionmark")
                                .font(.footnote).foregroundStyle(.secondary)
                        }
                        if summary.legacyReceivedCount > 0 {
                            Label("旧OEの\(summary.legacyReceivedCount)件は受信時刻で表示しています。", systemImage: "iphone.and.arrow.forward")
                                .font(.footnote).foregroundStyle(.secondary)
                        }
                    }
                }
                HistoryPlotModeSelector(mode: $plotMode)
                Toggle("縦軸を固定", isOn: $isYAxisFixed)
                    .frame(minHeight: 44)
                    .disabled(transformStart != nil)
                    .accessibilityHint("現在の各項目の目盛りを保ちます。解除すると表示範囲に合わせて自動調整します。")
                if plotMode == .overlay {
                    Button { sheet = .axes } label: {
                        Label("目盛りを選ぶ（最大3項目）", systemImage: "chart.bar.xaxis")
                            .frame(minHeight: 44)
                    }
                    .buttonStyle(.plain)
                }
                LazyVStack(spacing: 16) {
                    if plotMode == .overlay {
                        HistoryOverlayChartView(metrics: metrics, series: rendered?.series ?? [:], scales: rendered?.scales ?? [:],
                            requestedAxes: metrics.filter { selectedAxes.contains($0) }, range: displayRange,
                            selectedTime: $selectedTime,
                            highlightedSample: selectedSample ?? (selectedTime == nil ? readings.last : nil),
                            onStepSelection: stepSelection, onTransform: handleTransform,
                            isTransformActive: { transformStart != nil })
                    } else {
                        ForEach(metrics) { metric in
                            HistoryChartView(series: rendered?.series[metric], metric: metric, range: displayRange,
                                             selectedTime: $selectedTime,
                                             highlightedSample: selectedSample ?? (selectedTime == nil ? readings.last : nil),
                                             onStepSelection: stepSelection, onTransform: handleTransform,
                                             isTransformActive: { transformStart != nil })
                        }
                    }
                }
                Text("2本指で拡大・縮小と時間移動。1本指の横移動は時刻選択、縦移動はスクロールです。")
                    .font(.footnote).foregroundStyle(.secondary)
                Text("表示は\(viewport.bucketSeconds)秒ごとの最後の値です。拡大すると1秒の記録まで確認できます。保存した元データは間引きません。欠測・再起動と、\(Int(viewport.maximumGapSeconds))秒を超える間隔で線を区切ります。")
                    .font(.footnote).foregroundStyle(.secondary)
                Text(loadError ?? "")
                    .font(.footnote).foregroundStyle(.secondary).lineLimit(2, reservesSpace: true)
                VStack(alignment: .leading, spacing: 10) {
                    Text("この期間を書き出す").font(.headline)
                    HistoryExportView(model: model, deviceID: deviceID, range: viewport.range)
                }
            }
            .padding()
        }
        .background(.quaternary.opacity(0.25))
        .scrollDisabled(transformStart != nil)
        .navigationTitle(model.deviceName(deviceID))
        .navigationBarTitleDisplayMode(.inline)
        .overlay(alignment: .topTrailing) {
            ProgressView().padding(12).opacity(isLoading ? 1 : 0)
                .accessibilityHidden(!isLoading).allowsHitTesting(false)
        }
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                Button("測定項目を選ぶ", systemImage: "slider.horizontal.3") { sheet = .metrics }
            }
        }
        .sheet(item: $sheet) { item in
            switch item {
            case .metrics: HistoryMetricSelectionView(selected: $selectedMetrics)
            case .timeRange: HistoryTimeRangeSheet(viewport: viewport) { followsLatest = false; viewport = $0 }
            case .axes: HistoryAxisSelectionView(metrics: metrics, selected: $selectedAxes)
            }
        }
        .task(id: deviceID) { await initialize() }
        .task(id: reloadRevision) {
            guard initialized, transformStart == nil else { return }
            let revision = reloadRevision
            isLoading = true
            lastReloadStarted = .now
            try? await Task.sleep(for: .milliseconds(200))
            guard !Task.isCancelled else { return }
            await reload(revision: revision)
        }
        .onChange(of: model.historyRevision) { requestReload() }
        .onChange(of: viewport) {
            automaticReload?.cancel()
            automaticReload = nil
            if let selectedTime, !viewport.range.contains(selectedTime) { self.selectedTime = nil }
            needsReload = false
            reloadRevision += 1
        }
        .onChange(of: selectedMetrics) {
            selectedAxes.formIntersection(selectedMetrics)
            if selectedAxes.isEmpty { selectedAxes = Set(metrics.prefix(3)) }
        }
        .onChange(of: selectedTime) {
            if selectedTime != nil { followsLatest = false }
            updateSelection()
        }
        .onChange(of: isYAxisFixed) {
            fixedScales = isYAxisFixed ? rendered?.scales ?? [:] : [:]
            automaticReload?.cancel()
            automaticReload = nil
            needsReload = false
            reloadRevision += 1
        }
        .refreshable {
            model.refresh()
            automaticReload?.cancel()
            automaticReload = nil
            needsReload = false
            reloadRevision += 1
        }
        .onDisappear {
            automaticReload?.cancel()
            automaticReload = nil
            transformStart = nil
            transformBounds = nil
            previewViewport = nil
        }
    }

    private var metrics: [SensorMetric] { SensorMetric.allCases.filter { selectedMetrics.contains($0) } }
    private var readings: [StoredReading] { rendered?.readings ?? [] }
    private var latestBound: Date { model.isDemo ? available?.upperBound ?? .now : .now }
    private var movementBounds: ClosedRange<Date> {
        let upper = max(latestBound, available?.upperBound ?? latestBound)
        return min(available?.lowerBound ?? viewport.start, upper)...upper
    }
    private var displayRange: ClosedRange<Date> {
        (previewViewport ?? viewport).range
    }

    private func initialize() async {
        initialized = false
        do {
            available = try await model.loadMeasurementRange(deviceID: deviceID)
            guard !Task.isCancelled else { return }
            viewport = HistoryViewport.initial(now: .now, latestMeasurement: available?.upperBound, isDemo: model.isDemo)
            initialized = true
            reloadRevision += 1
        } catch is CancellationError {
        } catch {
            loadError = error.localizedDescription
        }
    }

    private func reload(revision: Int) async {
        let requestedViewport = viewport
        let requestedDevice = deviceID
        let requestedFixedScales = isYAxisFixed ? fixedScales : nil
        defer {
            if revision == reloadRevision {
                isLoading = false
                if needsReload {
                    needsReload = false
                    requestReload()
                }
            }
        }
        do {
            let data = try await model.loadHistorySnapshot(deviceID: requestedDevice,
                since: requestedViewport.start, until: requestedViewport.end,
                bucketSeconds: requestedViewport.bucketSeconds)
            try Task.checkCancellation()
            let previous = rendered
            let preparation = Task.detached(priority: .utility) {
                try HistoryRenderSnapshot.prepare(readings: data.readings, viewport: requestedViewport,
                                                  fixedScales: requestedFixedScales,
                                                  previous: previous)
            }
            let prepared = try await withTaskCancellationHandler {
                try await preparation.value
            } onCancel: { preparation.cancel() }
            guard !Task.isCancelled, revision == reloadRevision, requestedViewport == viewport,
                  requestedDevice == deviceID, transformStart == nil,
                  requestedFixedScales == (isYAxisFixed ? fixedScales : nil) else { return }
            available = data.availableRange
            summary = data.timeSummary
            rendered = prepared
            if isYAxisFixed { fixedScales = prepared.fixedScales ?? fixedScales }
            loadError = nil
            updateSelection()
        } catch is CancellationError {
        } catch {
            if revision == reloadRevision { loadError = error.localizedDescription }
        }
    }

    /// Save notifications coalesce while work is running. They never cancel it.
    private func requestReload() {
        guard initialized else { return }
        needsReload = true
        guard transformStart == nil else { return }
        guard !isLoading, automaticReload == nil else { return }
        let elapsed = lastReloadStarted?.duration(to: .now) ?? .seconds(3)
        let delay = max(.zero, .seconds(3) - elapsed)
        automaticReload = Task {
            do { try await Task.sleep(for: delay) } catch { return }
            guard !Task.isCancelled else { return }
            automaticReload = nil
            guard !isLoading else { return }
            needsReload = false
            if followsLatest { viewport = viewport.ending(at: latestBound) }
            reloadRevision += 1
        }
    }

    private func updateSelection() {
        selectedSample = selectedTime.flatMap {
            rendered?.selection.nearest(to: $0,
                maximumDistance: max(Double(viewport.bucketSeconds) * 1.5,
                                     min(viewport.maximumGapSeconds, (rendered?.nominalInterval ?? 1) / 2 + 1)))
        }
    }

    private func applyPreset(_ preset: HistoryRangePreset) {
        viewport = HistoryViewport(endingAt: viewport.end, duration: preset.duration)
    }
    private func move(_ fraction: Double) { followsLatest = false; viewport = viewport.moved(fraction: fraction, within: movementBounds) }
    private func zoom(_ scale: Double) { followsLatest = false; viewport = viewport.zoomed(magnification: scale, anchor: selectedTime) }
    private func returnToLatest() { followsLatest = true; viewport = viewport.ending(at: latestBound); selectedTime = nil }

    private func stepSelection(_ step: Int) {
        selectedTime = rendered?.selection.adjacent(to: selectedSample?.id, step: step)?.reading.chartTimestamp
    }

    private func handleTransform(_ event: HistoryPlotTransform) {
        switch event.phase {
        case .began:
            guard transformStart == nil else { return }
            transformStart = viewport
            transformBounds = movementBounds
            previewViewport = viewport
            followsLatest = false
            needsReload = needsReload || isLoading || automaticReload != nil
            automaticReload?.cancel()
            automaticReload = nil
            // Cancel any in-flight snapshot, without starting a new query.
            isLoading = false
            reloadRevision += 1
        case .changed, .ended:
            guard let start = transformStart, let bounds = transformBounds else { return }
            let changed = start.transformed(initialCentroidX: event.initialCentroidX,
                currentCentroidX: event.currentCentroidX, plotWidth: event.plotWidth,
                magnification: event.magnification, within: bounds)
            if event.phase == .changed {
                previewViewport = changed
            } else {
                transformStart = nil
                transformBounds = nil
                previewViewport = nil
                if viewport != changed { viewport = changed }
                else if needsReload { requestReload() }
            }
        case .cancelled:
            transformStart = nil
            transformBounds = nil
            previewViewport = nil
            if needsReload { requestReload() }
        }
    }
}
