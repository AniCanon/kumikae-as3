import Testing

@testable import SimulatorBridge

struct FrameCadenceTests {
  @Test func copyingAndPublishingConsumeThePollingBudgetWithoutCatchUpBursts() {
    #expect(BridgeRunner.frameSleepNanos(started: 100, now: 100) == 8_000_000)
    #expect(BridgeRunner.frameSleepNanos(started: 100, now: 6_000_100) == 2_000_000)
    #expect(BridgeRunner.frameSleepNanos(started: 100, now: 8_000_100) == 0)
    #expect(BridgeRunner.frameSleepNanos(started: 100, now: 20_000_100) == 0)
  }
}
