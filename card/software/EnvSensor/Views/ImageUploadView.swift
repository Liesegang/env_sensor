import PhotosUI
import SwiftUI

struct ImageUploadView: View {
    let model: SensorAppModel
    let device: DeviceRecord
    @Environment(\.dismiss) private var dismiss
    @State private var selection: PhotosPickerItem?
    @State private var source: CardImageSource?
    @State private var adjustments = CardImageAdjustments()
    @State private var prepared: PreparedCardImage?
    @State private var preparedAdjustments: CardImageAdjustments?
    @State private var renderRevision = UUID()
    @State private var loading = false
    @State private var preparing = false
    @State private var conversionError: String?
    @State private var showingPlacement = false

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                VStack(alignment: .leading, spacing: 12) {
                    PhotosPicker("写真を選ぶ", selection: $selection, matching: .images,
                                 preferredItemEncoding: .current)
                        .buttonStyle(.bordered)
                        .disabled(isBusy || loading)
                    if loading { ProgressView("写真を読み込み中") }
                    if source != nil {
                        Button("配置・トリミングを編集", systemImage: "crop") { showingPlacement = true }
                            .buttonStyle(.borderedProminent).disabled(isBusy)
                        if let prepared {
                            CardImageResultPreview(prepared: prepared,
                                                   updating: preparing || preparedAdjustments != adjustments)
                                .frame(maxWidth: 600).frame(maxWidth: .infinity)
                        } else if preparing { ProgressView("6色に変換中") }
                        Text("6色の仕上がりです。元の写真の位置・大きさは編集画面で調整できます。")
                            .font(.caption).foregroundStyle(.secondary)
                    } else if preparing {
                        ProgressView("6色に変換中")
                    } else if source == nil, !loading {
                        Text("写真を選び、回転・位置・倍率と色を調整して\(device.name)へ送信できます。")
                            .font(.subheadline).foregroundStyle(.secondary)
                    }
                }
                .padding()
                ScrollView {
                    VStack(alignment: .leading, spacing: 20) {
                        if source != nil {
                            CardImageAdjustmentControls(adjustments: $adjustments)
                                .disabled(isBusy)
                            Text("プレビューと電子ペーパーでは、色の見え方が異なります。")
                                .font(.caption).foregroundStyle(.secondary)
                        }
                        if let conversionError { Text(conversionError).foregroundStyle(.red) }
                        if phase != .idle {
                            Text(phase.title).font(.headline)
                            if case .sending(let fraction) = phase {
                                ProgressView(value: fraction)
                                    .accessibilityLabel("画像転送の進捗")
                            } else if phase.isBusy { ProgressView() }
                        }
                        if model.collector?.imageTransfer.isStopping == true {
                            Text("前の転送の終了を確認しています。再接続後に送信を始めます。")
                                .font(.footnote).foregroundStyle(.secondary)
                        }
                        Button("cardへ送信", systemImage: "arrow.up.doc", action: send)
                            .buttonStyle(.borderedProminent)
                            .disabled(prepared == nil || preparedAdjustments != adjustments || loading || preparing || isBusy || !canSend)
                        if model.collector?.imageTransfer.canCancel == true {
                            Button("転送を中止", role: .destructive) { model.collector?.imageTransfer.cancel() }
                        }
                        if !canSend {
                            Text(model.isDemo ? "表示テストでは画像を送信できません。" : model.collector?.imageSendingUnavailableReason(for: device.id) ?? "Bluetoothの接続を確認してください。")
                                .font(.footnote).foregroundStyle(.secondary)
                        }
                        Text("送信を押すと、次の通信で自動的に転送を始めます。cardの画面が更新されるまでアプリを開いておいてください。待機・送信中は中止できます。保存・画面更新が始まると処理は本体で続きます。")
                            .font(.footnote).foregroundStyle(.secondary)
                    }
                    .padding()
                }
            }
            .navigationTitle("電子ペーパーの画像")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) { Button("閉じる", action: close) }
            }
            .task(id: selection) { await loadPhoto() }
            .task(id: renderRevision) { await preparePreview() }
            .onChange(of: adjustments) { renderRevision = UUID() }
            .sheet(isPresented: $showingPlacement) {
                if let source {
                    CardImagePlacementEditor(source: source, prepared: prepared,
                                             updating: preparing || preparedAdjustments != adjustments,
                                             adjustments: $adjustments)
                        .presentationDetents([.large])
                        .presentationDragIndicator(.visible)
                }
            }
        }
    }

    private var phase: CardImageTransferPhase {
        guard let transfer = model.collector?.imageTransfer, transfer.deviceID == device.id else { return .idle }
        return transfer.phase
    }
    private var canSend: Bool { model.collector?.canSendImage(to: device.id) == true }
    // A request can be queued while an older transfer's connection is ending.
    // Its BEGIN stays fenced until that actual disconnection has completed.
    private var isBusy: Bool { model.collector?.imageTransfer.phase.isBusy == true }

    private func loadPhoto() async {
        guard let selection else { return }
        loading = true
        source = nil
        prepared = nil
        preparedAdjustments = nil
        renderRevision = UUID()
        conversionError = nil
        do {
            guard let photo = try await selection.loadTransferable(type: Data.self) else {
                throw CardImageError.decoding
            }
            let worker = Task.detached(priority: .userInitiated) { try CardImageConverter.decode(photo) }
            let decoded = try await withTaskCancellationHandler {
                try await worker.value
            } onCancel: { worker.cancel() }
            try Task.checkCancellation()
            guard self.selection == selection else { return }
            source = decoded
            adjustments = CardImageAdjustments()
            loading = false
            renderRevision = UUID()
        } catch is CancellationError {
            return
        } catch {
            guard !Task.isCancelled, self.selection == selection else { return }
            loading = false
            conversionError = error.localizedDescription
        }
    }

    private func preparePreview() async {
        guard let source else { preparing = false; return }
        let revision = renderRevision
        let requested = adjustments
        preparing = true
        conversionError = nil
        do {
            // Slider and gesture changes cancel this task before expensive work.
            try await Task.sleep(for: .milliseconds(120))
            let worker = Task.detached(priority: .userInitiated) {
                try CardImageConverter.prepare(source, adjustments: requested)
            }
            let converted = try await withTaskCancellationHandler {
                try await worker.value
            } onCancel: { worker.cancel() }
            try Task.checkCancellation()
            guard renderRevision == revision, adjustments == requested else { return }
            prepared = converted
            preparedAdjustments = requested
            preparing = false
        } catch is CancellationError {
            return
        } catch {
            guard !Task.isCancelled, renderRevision == revision else { return }
            preparing = false
            conversionError = error.localizedDescription
        }
    }

    private func send() {
        if let prepared, preparedAdjustments == adjustments, !preparing, !loading {
            model.collector?.sendImage(prepared.frame, to: device.id)
        }
    }
    private func close() {
        if phase.isBusy { model.collector?.imageTransfer.cancel() }
        dismiss()
    }
}
