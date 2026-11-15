import Darwin
import Foundation
import XPC

// Input is part of the same private adapter, separated from device and framebuffer lookup.
extension AppleSimulator {
  public func send(_ command: InputCommand) async throws {
    let started = DispatchTime.now().uptimeNanoseconds
    activeCommandID = command.id
    metrics?.inputTrace.mark(id: command.id, phase: .actor, at: started)
    defer {
      activeCommandID = nil
      metrics?.record("inputExecuteNs", DispatchTime.now().uptimeNanoseconds - started)
    }
    let operation = try command.operation
    switch operation {
    case .ping: return
    case .down, .move, .up:
      guard !rotationRequested else {
        throw BridgeError.invalid("Reconnect the picture after rotation")
      }
      guard let x = command.x, let y = command.y, x.isFinite, y.isFinite,
        x >= 0, y >= 0, x < Double(info.width) / info.density,
        y < Double(info.height) / info.density
      else {
        throw BridgeError.invalid("Touch coordinates are outside the Simulator screen")
      }
      guard (operation == .down) == (finger == nil) else {
        throw BridgeError.invalid("Touch phases arrived out of order")
      }
      try await touch(command.op, x, y)
    case .home, .lock:
      for state in NativeInputValues.keyPress {
        try await button(operation == .home ? 0 : 1, state: state)
      }
    case .rotate:
      guard let orientation = command.orientation else {
        throw BridgeError.invalid("Orientation is missing")
      }
      try await rotate(orientation)
    case .paste:
      for event in PasteKeyboard.events {
        try await key(event.usage, state: event.state)
      }
    case .text:
      guard let text = command.text else {
        throw BridgeError.invalid("Text is missing")
      }
      let keys = try ASCIIKeyboard.keys(for: text)
      for press in keys {
        if press.shift {
          try await key(NativeInputValues.leftShift, state: NativeInputValues.pressed)
        }
        for state in NativeInputValues.keyPress {
          try await key(press.usage, state: state)
        }
        if press.shift {
          try await key(NativeInputValues.leftShift, state: NativeInputValues.released)
        }
      }
    }
  }

  private func touch(_ phase: String, _ x: Double, _ y: Double) async throws {
    guard let frameRotation else {
      throw BridgeError.unavailable("No oriented Simulator picture is ready")
    }
    let point = frameRotation.sourcePoint(
      x: x * info.density / Double(info.width), y: y * info.density / Double(info.height))
    let message = try Self.touchMessage(phase: phase, x: point.0, y: point.1)
    if phase != "up" {
      finger = (x, y)
    }
    try await deliver(message)
    if phase == "up" {
      finger = nil
    }
  }

  private func deliver(_ message: xpc_object_t) async throws {
    guard let hid else {
      throw BridgeError.unavailable("Simulator input is disconnected")
    }
    if let failure = inputFailure.withLock({ $0 }) {
      throw BridgeError.unavailable(failure)
    }
    xpc_connection_send_message(hid, message)
    try await inputBarrier()
  }

  public func disconnect() async {
    if let (x, y) = finger {
      do {
        try await touch("up", x, y)
      } catch {
        reportReleaseFailure(error)
      }
    }
    for usage in heldKeys.sorted() {
      do {
        try await key(usage, state: NativeInputValues.released)
      } catch {
        reportReleaseFailure(error)
      }
    }
    if let heldButton {
      do {
        try await button(heldButton, state: NativeInputValues.released)
      } catch {
        reportReleaseFailure(error)
      }
    }
    if let orientationConnection {
      xpc_connection_cancel(orientationConnection)
    }
    orientationConnection = nil
    if let hid {
      xpc_connection_cancel(hid)
    }
    hid = nil
    display = nil
    device = nil
    finger = nil
    heldKeys.removeAll()
    heldButton = nil
  }

  private func key(_ usage: UInt32, state: UInt32) async throws {
    if state == NativeInputValues.pressed {
      heldKeys.insert(usage)
    }
    try await deliver(
      Self.buttonMessage(usage: UInt64(usage), state: UInt64(state), keyboard: true))
    if state == NativeInputValues.released {
      heldKeys.remove(usage)
    }
  }

  private func button(_ kind: UInt32, state: UInt32) async throws {
    if state == NativeInputValues.pressed {
      heldButton = kind
    }
    try await deliver(
      Self.buttonMessage(usage: kind == 0 ? 64 : 48, state: UInt64(state), keyboard: false))
    if state == NativeInputValues.released {
      heldButton = nil
    }
  }

  private func reportReleaseFailure(_ error: any Error) {
    FileHandle.standardError.write(
      Data("Unable to release Simulator input: \(error.localizedDescription)\n".utf8))
  }
}
