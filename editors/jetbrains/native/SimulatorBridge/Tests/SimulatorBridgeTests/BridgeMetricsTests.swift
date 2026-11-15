import Foundation
import Testing

@testable import SimulatorBridge

struct BridgeMetricsTests {
  @Test func recordsBoundedAggregatesAndResetsTheWindow() throws {
    let path = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    defer { try? FileManager.default.removeItem(at: path) }
    let metrics = try BridgeMetrics(path: path.path)
    metrics.record("sampleNs", 7)
    metrics.record("sampleNs", 11)
    try metrics.flush(force: true)
    try metrics.flush(force: true)
    let lines = try String(contentsOf: path, encoding: .utf8).split(separator: "\n")
    let first = try #require(
      try JSONSerialization.jsonObject(with: Data(lines[0].utf8)) as? [String: Any])
    let start = try #require(first["startNanos"] as? UInt64)
    let end = try #require(first["endNanos"] as? UInt64)
    #expect(end > start)
    #expect(first["nanos"] as? UInt64 == end - start)
    let values = try #require(first["metrics"] as? [String: [String: Int]])
    #expect(values["sampleNs"] == ["count": 2, "total": 18, "maximum": 11])
    let second = try #require(
      try JSONSerialization.jsonObject(with: Data(lines[1].utf8)) as? [String: Any])
    #expect((second["metrics"] as? [String: [String: Int]])?.isEmpty == true)
    #expect(throws: (any Error).self) { try BridgeMetrics(path: path.path) }
  }
}
