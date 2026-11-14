import Darwin
import Foundation

public protocol CommandSource: Sendable {
  func next() async throws -> InputCommand?
  func reply(id: Int, receivedNanos: UInt64, error: String?) async throws
}

public actor CommandChannel: CommandSource {
  private var pending = Data()
  private let descriptor: Int32

  public init(descriptor: Int32 = STDIN_FILENO) throws {
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
        guard line.count <= 16384 else { throw BridgeError.invalid("Bridge command is too large") }
        let command = try JSONDecoder().decode(InputCommand.self, from: line)
        guard command.id > 0 else {
          throw BridgeError.invalid("Bridge command id must be positive")
        }
        return command
      }
      guard pending.count <= 16384 else { throw BridgeError.invalid("Bridge command is too large") }
      var bytes = [UInt8](repeating: 0, count: 4096)
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
      try await Task.sleep(for: .milliseconds(2))
    }
    throw CancellationError()
  }

  public func reply(id: Int, receivedNanos: UInt64, error: String?) throws {
    var reply: [String: Any] = ["id": id, "receivedNanos": receivedNanos, "ok": error == nil]
    if let error { reply["error"] = error }
    try Self.write(reply)
  }

  public static func write(_ value: [String: Any]) throws {
    var bytes = try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys])
    bytes.append(10)
    try FileHandle.standardOutput.write(contentsOf: bytes)
  }
}
