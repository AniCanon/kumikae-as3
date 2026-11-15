import Testing

@testable import SimulatorBridge

struct RunnerFailureTests {
  actor FailingReply: CommandSource {
    var delivered = false
    var replies = 0

    func next() async throws -> InputCommand? {
      if !delivered {
        delivered = true
        return InputCommand(id: 1, op: "ping", x: nil, y: nil, text: nil)
      }
      try await Task.sleep(for: .seconds(60))
      return nil
    }

    func reply(id: Int, receivedNanos: UInt64, error: String?) throws {
      replies += 1
      throw BridgeError.invalid("Reply pipe failed")
    }
  }

  struct FailingCommands: CommandSource {
    func next() throws -> InputCommand? { throw BridgeError.invalid("Command pipe failed") }
    func reply(id: Int, receivedNanos: UInt64, error: String?) {}
  }

  actor FailingClose: FrameSink {
    var closes = 0
    func publish(_ frame: CapturedFrame) {}
    func close() throws {
      closes += 1
      throw BridgeError.invalid("Frame close failed")
    }
  }

  @Test func failedReplyIsNotRetriedAsAnInputFailure() async throws {
    let device = LifetimeTests.FakeDevice()
    let sink = LifetimeTests.Sink()
    let commands = FailingReply()
    await #expect(throws: BridgeError.self) {
      try await BridgeRunner(
        device: device, source: device, sink: sink, input: device, commands: commands
      ).run()
    }
    #expect(await commands.replies == 1)
    #expect(await device.closed)
    #expect(await sink.closed)
  }

  @Test func cleanupFailureKeepsTheOriginalFailure() async {
    let device = LifetimeTests.FakeDevice()
    let sink = FailingClose()
    do {
      try await BridgeRunner(
        device: device, source: device, sink: sink, input: device, commands: FailingCommands()
      ).run()
      Issue.record("Expected failure")
    } catch {
      #expect(error.localizedDescription.contains("Command pipe failed"))
      #expect(error.localizedDescription.contains("Frame close failed"))
    }
    #expect(await device.closed)
    #expect(await sink.closes == 1)
  }
}
