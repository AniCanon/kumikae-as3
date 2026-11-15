import Foundation

public enum BridgeError: Error, LocalizedError, Sendable {
  case invalid(String)
  case unavailable(String)
  case deviceUnavailable(String)
  case system(String, Int32)

  public var code: String {
    if case .deviceUnavailable = self {
      return "DEVICE_UNAVAILABLE"
    }
    return "INPUT_UNAVAILABLE"
  }

  public var errorDescription: String? {
    switch self {
    case .invalid(let message), .unavailable(let message), .deviceUnavailable(let message): message
    case .system(let operation, let code): "\(operation) failed: \(String(cString: strerror(code)))"
    }
  }
}
