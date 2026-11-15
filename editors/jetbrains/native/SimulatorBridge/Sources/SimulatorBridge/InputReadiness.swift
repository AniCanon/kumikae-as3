import Foundation

// Only the empty startup barrier belongs here. Never retry an actual input event.
enum InputReadiness {
  static func check(_ barrier: @Sendable () async throws -> Void) async throws {
    do {
      try await barrier()
    } catch is CancellationError {
      throw CancellationError()
    } catch {
      throw BridgeError.deviceUnavailable(
        "The Simulator input service is not ready. Reconnect its picture. \(error.localizedDescription)"
      )
    }
  }
}
