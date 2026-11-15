import Foundation
import XPC

extension AppleSimulator {
  static let orientationService = "com.apple.coredevice.feature.remote.devicecontrol.orientation"

  static func orientationMessage(_ name: String) throws -> xpc_object_t {
    let value = try orientationValue(name)
    let change = xpc_dictionary_create_empty()
    xpc_dictionary_set_string(change, "_0", value)
    let payload = xpc_dictionary_create_empty()
    xpc_dictionary_set_value(payload, "changeOrientation", change)
    let message = xpc_dictionary_create_empty()
    xpc_dictionary_set_value(message, "payload", payload)
    return message
  }

  static func orientationValue(_ name: String) throws -> String {
    guard
      let value = [
        "portrait": "portrait", "portrait_upside_down": "portraitUpsideDown",
        "landscape_left": "landscapeLeft", "landscape_right": "landscapeRight",
      ][name]
    else {
      throw BridgeError.invalid("Invalid Simulator orientation")
    }
    return value
  }

  static func validateOrientationReply(_ reply: xpc_object_t, expected: String) throws {
    guard xpc_get_type(reply) == XPC_TYPE_DICTIONARY,
      xpc_dictionary_get_value(reply, "error") == nil,
      let actual = xpc_dictionary_get_string(reply, "currentDeviceOrientation"),
      String(cString: actual) == (try orientationValue(expected))
    else {
      throw BridgeError.unavailable("Simulator rotation returned an invalid reply")
    }
  }

  func rotate(_ name: String) async throws {
    let message = try Self.orientationMessage(name)
    guard finger == nil, let device else {
      throw BridgeError.invalid("Rotation needs an idle connected device")
    }
    rotationRequested = true
    if orientationConnection == nil {
      orientationConnection = try openConnection(device, service: Self.orientationService)
    }
    guard let connection = orientationConnection else {
      throw BridgeError.unavailable("Orientation service is unavailable")
    }
    let (replies, continuation) = AsyncStream<String?>.makeStream(
      bufferingPolicy: .bufferingNewest(1))
    xpc_connection_send_message_with_reply(
      connection, message, DispatchQueue.global(qos: .userInteractive)
    ) { reply in
      do {
        try Self.validateOrientationReply(reply, expected: name)
        continuation.yield(nil)
      } catch {
        continuation.yield(error.localizedDescription)
      }
      continuation.finish()
    }
    try await withThrowingTaskGroup(of: Void.self) { group in
      group.addTask {
        for await failure in replies {
          if let failure {
            throw BridgeError.unavailable("Simulator rotation failed: \(failure)")
          }
          return
        }
        throw CancellationError()
      }
      group.addTask {
        try await Task.sleep(for: .seconds(3))
        throw BridgeError.unavailable("Simulator rotation timed out; delivery is unknown")
      }
      defer {
        group.cancelAll()
        continuation.finish()
      }
      try await group.next()
    }
  }
}
