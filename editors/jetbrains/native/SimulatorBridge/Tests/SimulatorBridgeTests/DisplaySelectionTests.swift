import Foundation
import Testing

@testable import SimulatorBridge

private class ScreenProperties: NSObject {
  @objc dynamic var uniqueId: String
  @objc dynamic var screenType: UInt64

  init(_ id: String, type: UInt64) {
    uniqueId = id
    screenType = type
  }
}

private class ScreenDescriptor: NSObject {
  @objc dynamic let screenProperties: ScreenProperties

  init(_ id: String, type: UInt64) {
    screenProperties = ScreenProperties(id, type: type)
  }
}

private class ScreenPort: NSObject {
  @objc dynamic let descriptor: NSObject

  init(_ descriptor: NSObject) { self.descriptor = descriptor }
}

struct DisplaySelectionTests {
  @Test func uuidBuiltInDisplayIsSelectedAmongExternalScreens() throws {
    let screen = ScreenDescriptor("runtime-generated-id", type: 0)
    let runtime = try RuntimeSymbols(guardAPI: PrivateAPIGuard(version: "test", build: "test"))
    let selected = try AppleSimulator.mainDisplay(
      in: [
        ScreenPort(NSObject()), ScreenPort(ScreenDescriptor("external", type: 2)),
        ScreenPort(screen), ScreenPort(ScreenDescriptor("resizable", type: 4)),
      ], runtime: runtime)
    let matches = selected === screen
    #expect(matches)
  }

  @Test func legacyBuiltInDisplayStillWorks() throws {
    let screen = ScreenDescriptor("PurpleMain", type: 0)
    let runtime = try RuntimeSymbols(guardAPI: PrivateAPIGuard(version: "test", build: "test"))
    let selected = try AppleSimulator.mainDisplay(in: [ScreenPort(screen)], runtime: runtime)
    let matches = selected === screen
    #expect(matches)
  }

  @Test func externalDisplayIsNeverUsedAsThePhoneScreen() throws {
    let runtime = try RuntimeSymbols(guardAPI: PrivateAPIGuard(version: "test", build: "test"))
    #expect(throws: BridgeError.self) {
      try AppleSimulator.mainDisplay(
        in: [ScreenPort(ScreenDescriptor("PurpleMain", type: 2))], runtime: runtime)
    }
  }

  @Test func ambiguousBuiltInDisplaysAreRejected() throws {
    let runtime = try RuntimeSymbols(guardAPI: PrivateAPIGuard(version: "test", build: "test"))
    #expect(throws: BridgeError.self) {
      try AppleSimulator.mainDisplay(
        in: [
          ScreenPort(ScreenDescriptor("first", type: 0)),
          ScreenPort(ScreenDescriptor("second", type: 0)),
        ], runtime: runtime)
    }
  }
}
