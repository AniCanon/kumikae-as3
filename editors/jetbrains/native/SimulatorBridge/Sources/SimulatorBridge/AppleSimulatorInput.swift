import Darwin
import Foundation
import XPC

// Input is part of the same private adapter, separated from device and framebuffer lookup.
extension AppleSimulator {
  public func send(_ command: InputCommand) async throws {
    switch command.op {
    case "ping": return
    case "down", "move", "up":
      guard let x = command.x, let y = command.y, x.isFinite, y.isFinite,
        x >= 0, y >= 0, x < Double(info.width) / info.density,
        y < Double(info.height) / info.density
      else { throw BridgeError.invalid("Touch coordinates are outside the Simulator screen") }
      guard (command.op == "down") == (finger == nil) else {
        throw BridgeError.invalid("Touch phases arrived out of order")
      }
      try await touch(command.op, x, y)
    case "home", "lock":
      for state: UInt32 in [1, 2] { try await button(command.op == "home" ? 0 : 1, state: state) }
    case "text":
      guard let text = command.text else { throw BridgeError.invalid("Text is missing") }
      let keys = try ASCIIKeyboard.keys(for: text)
      for press in keys {
        if press.shift { try await key(225, state: 1) }
        for state: UInt32 in [1, 2] {
          try await key(press.usage, state: state)
        }
        if press.shift { try await key(225, state: 2) }
      }
    default: throw BridgeError.invalid("Unsupported native input command: \(command.op)")
    }
  }

  private func touch(_ phase: String, _ x: Double, _ y: Double) async throws {
    let message = try Self.touchMessage(
      phase: phase, x: x * info.density / Double(info.width),
      y: y * info.density / Double(info.height))
    if phase != "up" { finger = (x, y) }
    try await deliver(message)
    if phase == "up" { finger = nil }
  }

  private func deliver(_ message: xpc_object_t) async throws {
    guard let hid else { throw BridgeError.unavailable("Simulator input is disconnected") }
    if let failure = inputFailure.withLock({ $0 }) { throw BridgeError.unavailable(failure) }
    xpc_connection_send_message(hid, message)
    try await inputBarrier()
  }

  public func disconnect() async {
    if let (x, y) = finger {
      do { try await touch("up", x, y) } catch {
        reportReleaseFailure(error)
      }
    }
    for usage in heldKeys.sorted() {
      do { try await key(usage, state: 2) } catch { reportReleaseFailure(error) }
    }
    if let heldButton {
      do { try await button(heldButton, state: 2) } catch { reportReleaseFailure(error) }
    }
    if let hid { xpc_connection_cancel(hid) }
    hid = nil
    display = nil
    device = nil
    finger = nil
    heldKeys.removeAll()
    heldButton = nil
  }

  private func key(_ usage: UInt32, state: UInt32) async throws {
    if state == 1 { heldKeys.insert(usage) }
    try await deliver(
      Self.buttonMessage(usage: UInt64(usage), state: UInt64(state), keyboard: true))
    if state == 2 { heldKeys.remove(usage) }
  }
  private func button(_ kind: UInt32, state: UInt32) async throws {
    if state == 1 { heldButton = kind }
    try await deliver(
      Self.buttonMessage(usage: kind == 0 ? 64 : 48, state: UInt64(state), keyboard: false))
    if state == 2 { heldButton = nil }
  }
  private func reportReleaseFailure(_ error: any Error) {
    FileHandle.standardError.write(
      Data("Unable to release Simulator input: \(error.localizedDescription)\n".utf8))
  }
}
