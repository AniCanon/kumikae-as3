import Foundation
import Testing

@testable import SimulatorBridge

struct PrivateAPIGuardTests {
  struct Loader: SymbolLookup {
    let missing: String?
    func contains(_ symbol: String) -> Bool { symbol != missing }
  }

  @Test func missingSymbolNamesXcodeAndSymbol() {
    let guardAPI = PrivateAPIGuard(version: "Xcode test", build: "known", supportedBuild: "known")
    do {
      try guardAPI.validate(["First", "Missing"], using: Loader(missing: "Missing"))
      Issue.record("Missing private symbol was accepted")
    } catch {
      #expect(error.localizedDescription.contains("Xcode test"))
      #expect(error.localizedDescription.contains("Missing"))
    }
  }

  @Test func untestedBuildFailsEvenIfEverySymbolExists() {
    let guardAPI = PrivateAPIGuard(version: "Xcode next", build: "new", supportedBuild: "known")
    #expect(throws: (any Error).self) {
      try guardAPI.validate(["Present"], using: Loader(missing: nil))
    }
  }

  @Test func changedMethodSignatureFailsClosed() {
    let guardAPI = PrivateAPIGuard(version: "Xcode test", build: "known", supportedBuild: "known")
    #expect(throws: (any Error).self) { try guardAPI.require(false, symbol: "selector: signature") }
  }
}
