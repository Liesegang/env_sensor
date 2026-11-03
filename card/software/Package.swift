// swift-tools-version: 6.2
import PackageDescription
import Foundation

let sourceRoot = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
    .appendingPathComponent("EnvSensor").path
let excludedSourceEntries = ((try? FileManager.default.contentsOfDirectory(atPath: sourceRoot)) ?? [])
    .filter { !["Models", "Protocol", "Storage", "Bluetooth", "Images", "App"].contains($0) }
// Core Bluetooth is available on the native test host as well as iOS. Including
// the transport type-checks its actual event path alongside the portable codecs.
let bluetoothSources = ["SensorPacketReceiver.swift", "BluetoothFailure.swift", "CardImageWriteFence.swift",
                        "CardImageCommitBoundary.swift", "CardImageUploadQueue.swift", "CardHistorySync.swift",
                        "CardImageTransfer.swift", "CoreBluetoothCollector.swift", "CardImageTransferPhase.swift",
                        "BluetoothAvailability.swift", "DiscoveredSensor.swift", "SensorConnectionState.swift",
                        "SensorClockSyncState.swift"]
let excludedBluetoothEntries = ((try? FileManager.default.contentsOfDirectory(
    atPath: URL(fileURLWithPath: sourceRoot).appendingPathComponent("Bluetooth").path)) ?? [])
    .filter { !bluetoothSources.contains($0) }.map { "Bluetooth/" + $0 }

let package = Package(
    name: "EnvSensorCore",
    platforms: [.iOS(.v26), .macOS(.v26)],
    products: [.library(name: "EnvSensorCore", targets: ["EnvSensorCore"])],
    targets: [
        .target(name: "EnvSensorCore", path: "EnvSensor",
                exclude: excludedSourceEntries + excludedBluetoothEntries + ["Models/SensorMetric.swift", "App/EnvSensorApp.swift"],
                sources: ["Models", "Protocol", "Storage", "Images", "App"] + bluetoothSources.map { "Bluetooth/" + $0 },
                linkerSettings: [.linkedLibrary("sqlite3")]),
        .testTarget(name: "EnvSensorCoreTests", dependencies: ["EnvSensorCore"], path: "CoreTests",
                    resources: [.copy("Fixtures")])
    ]
)
