import XPC

extension AppleSimulator {
  static let inputService = "com.apple.coredevice.feature.remote.hid.digitizer"

  static func message(_ type: String, payload: xpc_object_t? = nil, barrier: Bool = false)
    -> xpc_object_t
  {
    let message = xpc_dictionary_create_empty()
    xpc_dictionary_set_string(message, "messageType", type)
    xpc_dictionary_set_string(message, "featureIdentifier", inputService)
    xpc_dictionary_set_bool(message, "isBarrier", barrier)
    if let payload {
      xpc_dictionary_set_value(message, "payload", payload)
    }
    return message
  }

  static func touchMessage(phase: String, x: Double, y: Double) throws -> xpc_object_t {
    guard let event = ["down": UInt64(0), "move": 1, "up": 2][phase],
      x.isFinite, y.isFinite, (0...1).contains(x), (0...1).contains(y)
    else {
      throw BridgeError.invalid("Invalid native touch phase or position")
    }
    let point = xpc_dictionary_create_empty()
    xpc_dictionary_set_double(point, "x", x)
    xpc_dictionary_set_double(point, "y", y)
    let payload = xpc_dictionary_create_empty()
    xpc_dictionary_set_value(payload, "pointOne", point)
    xpc_dictionary_set_uint64(payload, "eventType", event)
    xpc_dictionary_set_uint64(payload, "edge", 0)
    xpc_dictionary_set_uint64(payload, "target", 0)
    return message("IndigoDigitizerEvent", payload: payload)
  }

  static func buttonMessage(usage: UInt64, state: UInt64, keyboard: Bool) -> xpc_object_t {
    let payload = xpc_dictionary_create_empty()
    xpc_dictionary_set_uint64(payload, "usageCode", usage)
    xpc_dictionary_set_uint64(payload, "state", state)
    if !keyboard {
      xpc_dictionary_set_uint64(payload, "usagePage", 12)
    }
    return message(keyboard ? "IndigoKeyboardButtonEvent" : "IndigoButtonEvent", payload: payload)
  }
}
