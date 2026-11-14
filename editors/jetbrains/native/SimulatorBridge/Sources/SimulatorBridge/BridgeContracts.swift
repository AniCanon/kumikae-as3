import Foundation

public struct DeviceInfo: Codable, Sendable {
  public let width: Int
  public let height: Int
  public let density: Double
}

public struct CapturedFrame: Sendable {
  public var header: FrameHeader
  public let pixels: Data
}

public struct InputCommand: Codable, Sendable {
  public let id: Int
  public let op: String
  public let x: Double?
  public let y: Double?
  public let text: String?
}

public protocol DeviceLookup: Sendable {
  func connect(device: String) async throws -> DeviceInfo
  func disconnect() async
}

public protocol FrameSource: Sendable {
  func nextFrame() async throws -> CapturedFrame?
}

public protocol FrameSink: Sendable {
  func publish(_ frame: CapturedFrame) async throws
  func close() async throws
}

public protocol InputSink: Sendable {
  func send(_ command: InputCommand) async throws
}
