import Foundation

public enum FrameStartup {
  public static func publish(from source: any FrameSource, to sink: any FrameSink) async throws {
    let deadline = ContinuousClock.now + .seconds(2)
    repeat {
      try Task.checkCancellation()
      if let frame = try await source.nextFrame() {
        try await sink.publish(frame)
        return
      }
      try await Task.sleep(for: .milliseconds(8))
    } while ContinuousClock.now < deadline
    throw BridgeError.unavailable(
      "The Simulator did not produce a complete frame within two seconds")
  }
}
