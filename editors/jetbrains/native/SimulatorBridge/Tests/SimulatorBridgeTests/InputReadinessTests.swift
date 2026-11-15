import Testing

@testable import SimulatorBridge

struct InputReadinessTests {
  @Test func startupBarrierFailureAllowsReconnectWithoutFallback() async {
    do {
      try await InputReadiness.check {
        throw BridgeError.unavailable("Simulator input timed out; delivery is unknown")
      }
      Issue.record("A failed startup barrier was accepted")
    } catch let error as BridgeError {
      #expect(error.code == "DEVICE_UNAVAILABLE")
      #expect(error.localizedDescription.contains("input timed out"))
    } catch {
      Issue.record("Unexpected startup error: \(error)")
    }
  }

  @Test func cancellationIsNotADeviceFailure() async {
    await #expect(throws: CancellationError.self) {
      try await InputReadiness.check { throw CancellationError() }
    }
  }

  @Test func readyServicePasses() async throws {
    try await InputReadiness.check {}
  }
}
