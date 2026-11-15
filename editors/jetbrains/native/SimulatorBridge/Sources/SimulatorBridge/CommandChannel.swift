import Darwin
import Foundation

public protocol CommandSource: Sendable {
  func next() async throws -> InputCommand?
  func reply(id: Int, receivedNanos: UInt64, error: String?) async throws
}

public actor CommandChannel: CommandSource {
  private let metrics: BridgeMetrics?
  private static let maximumCommandBytes = 16_384
  private static let readBufferBytes = 4_096
  private var pending = Data()
  private let descriptor: Int32

  public init(descriptor: Int32 = STDIN_FILENO, metrics: BridgeMetrics? = nil) throws {
    self.metrics = metrics
    self.descriptor = descriptor
    let flags = fcntl(descriptor, F_GETFL)
    guard flags >= 0, fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0 else {
      throw BridgeError.system("Opening the parent command pipe", errno)
    }
  }

  public func next() async throws -> InputCommand? {
    while !Task.isCancelled {
      if let end = pending.firstIndex(of: 10) {
        let line = pending.prefix(upTo: end)
        pending.removeSubrange(...end)
        guard line.count <= Self.maximumCommandBytes else {
          throw BridgeError.invalid("Bridge command is too large")
        }
        let command = try JSONDecoder().decode(InputCommand.self, from: line)
        guard command.id > 0 else {
          throw BridgeError.invalid("Bridge command id must be positive")
        }
        return command
      }
      guard pending.count <= Self.maximumCommandBytes else {
        throw BridgeError.invalid("Bridge command is too large")
      }
      var bytes = [UInt8](repeating: 0, count: Self.readBufferBytes)
      let count = Darwin.read(descriptor, &bytes, bytes.count)
      if count == 0 {
        guard pending.isEmpty else {
          throw BridgeError.invalid("The parent ended a partial bridge command")
        }
        return nil
      }
      if count > 0 {
        pending.append(contentsOf: bytes.prefix(count))
        continue
      }
      guard errno == EAGAIN || errno == EINTR else {
        throw BridgeError.system("Reading bridge commands", errno)
      }
      let sleepStart = DispatchTime.now().uptimeNanoseconds
      try await Task.sleep(for: .milliseconds(2))
      metrics?.record(
        "commandSleepLateNs",
        max(DispatchTime.now().uptimeNanoseconds - sleepStart, 2_000_000) - 2_000_000)
    }
    throw CancellationError()
  }

  public func reply(id: Int, receivedNanos: UInt64, error: String?) throws {
    try Self.write(
      CommandReply(
        id: id, receivedNanos: receivedNanos,
        completedNanos: DispatchTime.now().uptimeNanoseconds, error: error))
  }

  public static func write(_ value: some Encodable) throws {
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.sortedKeys]
    var bytes = try encoder.encode(value)
    bytes.append(10)
    try FileHandle.standardOutput.write(contentsOf: bytes)
  }
}
