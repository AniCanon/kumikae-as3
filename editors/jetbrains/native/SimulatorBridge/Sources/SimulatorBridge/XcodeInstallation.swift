import Foundation

public struct XcodeInstallation: Sendable {
  let developerDirectory: String
  let version: String
  let build: String

  public static func current() throws -> XcodeInstallation {
    let command = Process()
    let output = Pipe()
    command.executableURL = URL(fileURLWithPath: "/usr/bin/xcode-select")
    command.arguments = ["-p"]
    command.standardOutput = output
    command.standardError = FileHandle.standardError
    try command.run()
    let bytes = output.fileHandleForReading.readDataToEndOfFile()
    command.waitUntilExit()
    guard command.terminationStatus == 0, let selected = String(data: bytes, encoding: .utf8) else {
      throw BridgeError.unavailable(
        "Select a full Xcode installation with xcode-select before using native Simulator input.")
    }
    let directory =
      ProcessInfo.processInfo.environment["DEVELOPER_DIR"]
      ?? selected.trimmingCharacters(in: .whitespacesAndNewlines)
    let contents = URL(fileURLWithPath: directory).deletingLastPathComponent()
    let values =
      try PropertyListSerialization.propertyList(
        from: Data(contentsOf: contents.appendingPathComponent("version.plist")), format: nil)
      as? [String: Any]
    guard let build = values?["ProductBuildVersion"] as? String,
      let version = values?["CFBundleShortVersionString"] as? String
    else {
      throw BridgeError.unavailable("The selected Xcode version could not be read.")
    }
    return XcodeInstallation(
      developerDirectory: directory, version: "Xcode \(version)", build: build)
  }
}
