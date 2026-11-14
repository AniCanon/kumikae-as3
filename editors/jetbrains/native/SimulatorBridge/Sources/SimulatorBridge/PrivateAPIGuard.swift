import Foundation

public protocol SymbolLookup {
  func contains(_ symbol: String) -> Bool
}

public struct PrivateAPIGuard {
  let version: String
  let build: String
  let supportedBuild: String

  func validate(_ symbols: [String], using loader: any SymbolLookup) throws {
    guard build == supportedBuild else {
      throw BridgeError.unavailable(
        "Native Simulator input has not been validated for \(version) (\(build)). Use the fallback until the bridge is updated."
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
