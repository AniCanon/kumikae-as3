import Foundation

public protocol SymbolLookup {
  func contains(_ symbol: String) -> Bool
}

public struct PrivateAPIGuard {
  let version: String
  let build: String
  // Add a build only for a demonstrated incompatibility, with its actionable reason.
  static let deniedBuilds: [String: String] = [:]
  let deniedBuilds: [String: String]

  init(version: String, build: String, deniedBuilds: [String: String] = Self.deniedBuilds) {
    self.version = version
    self.build = build
    self.deniedBuilds = deniedBuilds
  }

  func validate(_ symbols: [String], using loader: any SymbolLookup) throws {
    if let reason = deniedBuilds[build] {
      throw BridgeError.unavailable(
        "Native Simulator input is disabled for \(version) (\(build)): \(reason). Use the fallback."
      )
    }
    for symbol in symbols { try require(loader.contains(symbol), symbol: symbol) }
  }

  func require(_ condition: Bool, symbol: String) throws {
    guard condition else {
      throw BridgeError.unavailable(
        "\(version) (\(build)) does not provide the expected \(symbol). Update the bridge or use the fallback."
      )
    }
  }
}
