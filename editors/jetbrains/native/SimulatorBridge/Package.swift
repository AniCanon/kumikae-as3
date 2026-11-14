// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "SimulatorBridge",
    platforms: [.macOS(.v14)],
    products: [
        .library(name: "SimulatorBridge", targets: ["SimulatorBridge"]),
        .executable(name: "simulator-bridge", targets: ["SimulatorBridgeCLI"]),
    ],
    targets: [
        .target(name: "SimulatorBridge", resources: [.process("Resources")]),
        .executableTarget(name: "SimulatorBridgeCLI", dependencies: ["SimulatorBridge"]),
        .testTarget(name: "SimulatorBridgeTests", dependencies: ["SimulatorBridge"]),
    ],
    swiftLanguageModes: [.v6]
)
