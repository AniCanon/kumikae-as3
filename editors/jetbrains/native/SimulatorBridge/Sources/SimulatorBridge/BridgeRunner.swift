import Foundation

public struct BridgeRunner: Sendable {
  let device: any DeviceLookup
  let source: any FrameSource
  let sink: any FrameSink
  let input: any InputSink
  let commands: any CommandSource

  public init(
    device: any DeviceLookup, source: any FrameSource, sink: any FrameSink,
    input: any InputSink, commands: any CommandSource
  ) {
    self.device = device
    self.source = source
    self.sink = sink
    self.input = input
    self.commands = commands
  }

  public func run() async throws {
    do {
      try await withThrowingTaskGroup(of: Void.self) { group in
        let (incoming, continuation) = AsyncStream<InputCommand>.makeStream(
          bufferingPolicy: .bufferingOldest(1))
        group.addTask {
          defer { continuation.finish() }
          while let command = try await commands.next() {
            if case .dropped = continuation.yield(command) {
              throw BridgeError.invalid(
                "Too many pending native input commands; wait for each reply")
            }
          }
        }
        group.addTask {
          while !Task.isCancelled {
            if let frame = try await source.nextFrame() { try await sink.publish(frame) }
            try await Task.sleep(for: .milliseconds(8))
          }
        }
        group.addTask {
          for await command in incoming {
            let received = DispatchTime.now().uptimeNanoseconds
            do {
              try await input.send(command)
              try await commands.reply(id: command.id, receivedNanos: received, error: nil)
            } catch is CancellationError { throw CancellationError() } catch {
              try await commands.reply(
                id: command.id, receivedNanos: received, error: error.localizedDescription)
            }
          }
        }
        defer {
          group.cancelAll()
          continuation.finish()
        }
        try await group.next()
      }
    } catch {
      await device.disconnect()
      do { try await sink.close() } catch {
        throw BridgeError.unavailable(
          "Bridge failed and its frame slot could not close: \(error.localizedDescription)")
      }
      throw error
    }
    await device.disconnect()
    try await sink.close()
  }
}
