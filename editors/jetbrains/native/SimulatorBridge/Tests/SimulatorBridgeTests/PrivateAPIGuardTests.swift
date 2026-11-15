import Foundation
import Testing

@testable import SimulatorBridge

struct PrivateAPIGuardTests {
  struct Loader: SymbolLookup {
    let missing: String?
    func contains(_ symbol: String) -> Bool { symbol != missing }
  }

  @Test func missingSymbolNamesXcodeAndSymbol() {
    let guardAPI = PrivateAPIGuard(version: "Xcode test", build: "known")
    do {
      try guardAPI.validate(["First", "Missing"], using: Loader(missing: "Missing"))
      Issue.record("Missing private symbol was accepted")
    } catch {
      #expect(error.localizedDescription.contains("Xcode test"))
      #expect(error.localizedDescription.contains("Missing"))
    }
  }

  @Test func unlistedBuildPassesWhenEverySymbolExists() throws {
    let guardAPI = PrivateAPIGuard(version: "Xcode next", build: "new")
    try guardAPI.validate(["Present"], using: Loader(missing: nil))
  }

  @Test func unlistedBuildStillNamesMissingCapability() {
    let guardAPI = PrivateAPIGuard(version: "Xcode next", build: "new")
    do {
      try guardAPI.validate(["AbsentExport"], using: Loader(missing: "AbsentExport"))
      Issue.record("Missing private symbol was accepted")
    } catch {
      #expect(error.localizedDescription.contains("Xcode next (new)"))
      #expect(error.localizedDescription.contains("AbsentExport"))
    }
  }

  @Test func deniedBuildNamesItsReasonEvenWhenCapabilitiesPass() {
    let guardAPI = PrivateAPIGuard(
      version: "Xcode test", build: "broken", deniedBuilds: ["broken": "Input loses releases"])
    do {
      try guardAPI.validate(["Present"], using: Loader(missing: nil))
      Issue.record("Denied build was accepted")
    } catch {
      #expect(error.localizedDescription.contains("Xcode test (broken)"))
      #expect(error.localizedDescription.contains("Input loses releases"))
    }
  }

  @Test func changedMethodSignatureFailsClosed() {
    let guardAPI = PrivateAPIGuard(version: "Xcode test", build: "known")
    #expect(throws: (any Error).self) { try guardAPI.require(false, symbol: "selector: signature") }
  }
}
