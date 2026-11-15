import Foundation

public struct BridgeRunner: Sendable {
  let metrics: BridgeMetrics?
  let device: any DeviceLookup
  let source: any FrameSource
  let sink: any FrameSink
  let input: any InputSink
  let commands: any CommandSource

  public init(
    device: any DeviceLookup, source: any FrameSource, sink: any FrameSink,
    input: any InputSink, commands: any CommandSource, metrics: BridgeMetrics? = nil
  ) {
    self.metrics = metrics
    self.device = device
    self.source = source
    self.sink = sink
    self.input = input
    self.commands = commands
  }

  static let frameIntervalNanos: UInt64 = 8_000_000
  static func frameSleepNanos(started: UInt64, now: UInt64) -> UInt64 {
    let elapsed = now >= started ? now - started : 0
    return frameIntervalNanos - min(elapsed, frameIntervalNanos)
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
            let started = DispatchTime.now().uptimeNanoseconds
            let frame = try await source.nextFrame()
            metrics?.record("sourceNs", DispatchTime.now().uptimeNanoseconds - started)
            if let frame {
              let publishStart = DispatchTime.now().uptimeNanoseconds
              try await sink.publish(frame)
              metrics?.record("publishNs", DispatchTime.now().uptimeNanoseconds - publishStart)
            }
            try metrics?.flush()
            let sleepStart = DispatchTime.now().uptimeNanoseconds
            let delay = Self.frameSleepNanos(started: started, now: sleepStart)
            if delay > 0 { try await Task.sleep(nanoseconds: delay) }
            else { await Task.yield() }
            metrics?.record("frameSleepLateNs", max(DispatchTime.now().uptimeNanoseconds - sleepStart, delay) - delay)
          }
        }
        group.addTask {
          for await command in incoming {
            let received = DispatchTime.now().uptimeNanoseconds
            metrics?.inputTrace.begin(id: command.id, at: received)
            do {
              try await input.send(command)
              metrics?.record("inputCallNs", DispatchTime.now().uptimeNanoseconds - received)
              metrics?.inputTrace.finish(id: command.id, at: DispatchTime.now().uptimeNanoseconds)
              try await commands.reply(id: command.id, receivedNanos: received, error: nil)
            } catch is CancellationError { throw CancellationError() } catch {
              metrics?.inputTrace.finish(id: command.id, at: DispatchTime.now().uptimeNanoseconds)
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
