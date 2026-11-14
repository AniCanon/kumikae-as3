import Foundation
import Testing

@testable import SimulatorBridge

struct FrameStartupTests {
  actor Source: FrameSource {
    var calls = 0
    func nextFrame() -> CapturedFrame? {
      calls += 1
      if calls == 1 { return nil }
      return CapturedFrame(
        header: FrameHeader(
          sequence: 1, width: 1, height: 1, stride: 4,
          observedNanos: 1, writtenNanos: 1, nativeWidth: 1, nativeHeight: 1, densityMilli: 1000),
        pixels: Data(repeating: 0, count: 4))
    }
  }
  actor Sink: FrameSink {
    var published = false
    func publish(_ frame: CapturedFrame) { published = true }
    func close() {}
  }
  @Test func startupWaitsForACompleteFrame() async throws {
    let sink = Sink()
    try await FrameStartup.publish(from: Source(), to: sink)
    #expect(await sink.published)
  }
}
