import Foundation
import ObjectiveC

extension AppleSimulator {
  static func mainDisplay(in ports: [AnyObject], runtime: RuntimeSymbols) throws -> AnyObject {
    var display: AnyObject?
    for port in ports {
      guard let descriptor = try runtime.object(port, "descriptor"),
        (descriptor as? NSObjectProtocol)?.responds(to: NSSelectorFromString("screenProperties"))
          == true,
        let properties = try runtime.object(descriptor, "screenProperties"),
        try runtime.integer(properties, "screenType", encoding: "Q") == 0
      else {
        continue
      }
      guard display == nil else {
        throw BridgeError.unavailable("Multiple built-in Simulator displays were reported")
      }
      display = descriptor
    }
    guard let display else {
      throw BridgeError.deviceUnavailable("No built-in Simulator display is ready")
    }
    return display
  }
}
