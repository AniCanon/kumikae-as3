import Testing

@testable import SimulatorBridge

struct LifetimeTests {
  actor SlowInput: InputSink {
    var started = false
    var completed = false
    func send(_ command: InputCommand) async throws {
      started = true
      try await Task.sleep(for: .milliseconds(30))
      completed = true
    }
  }
  actor ClosingCommands: CommandSource {
    let input: SlowInput
    var sent = false
    init(input: SlowInput) { self.input = input }
    func next() async -> InputCommand? {
      if !sent {
        sent = true
        return InputCommand(id: 1, op: "text", x: nil, y: nil, text: "test")
      }
      while !(await input.started) { await Task.yield() }
      return nil
    }
    func reply(id: Int, receivedNanos: UInt64, error: String?) {}
  }

  @Test func parentEOFInterruptsAnInputAlreadyInFlight() async throws {
    let device = FakeDevice()
    let sink = Sink()
    let input = SlowInput()
    try await BridgeRunner(
      device: device, source: device, sink: sink, input: input,
      commands: ClosingCommands(input: input)
    ).run()
    #expect(!(await input.completed))
    #expect(await device.closed)
    #expect(await sink.closed)
  }
  actor FakeDevice: DeviceLookup, FrameSource, InputSink {
    var closed = false
    func connect(device: String) -> DeviceInfo { DeviceInfo(width: 2, height: 2, density: 1) }
    func disconnect() { closed = true }
    func nextFrame() -> CapturedFrame? { nil }
    func send(_ command: InputCommand) {}
  }
  actor Sink: FrameSink {
    var closed = false
    func publish(_ frame: CapturedFrame) {}
    func close() { closed = true }
  }
  struct EOF: CommandSource {
    func next() -> InputCommand? { nil }
    func reply(id: Int, receivedNanos: UInt64, error: String?) {}
  }

  @Test func parentEOFClosesDeviceAndFrameSink() async throws {
    let device = FakeDevice()
    let sink = Sink()
    try await BridgeRunner(
      device: device, source: device, sink: sink, input: device, commands: EOF()
    ).run()
    #expect(await device.closed)
    #expect(await sink.closed)
  }
}
