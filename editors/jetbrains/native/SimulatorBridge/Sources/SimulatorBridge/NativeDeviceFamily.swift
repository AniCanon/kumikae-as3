import Foundation

enum NativeDeviceFamily {
  static func validate(_ family: Int32) throws {
    guard family == 1 || family == 2 else {
      throw BridgeError.unavailable("Native input is validated for iPhone and iPad simulators only")
    }
  }
}
