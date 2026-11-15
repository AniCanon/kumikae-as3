import Darwin
import Foundation
import ObjectiveC
import XPC

extension AppleSimulator {
  func connectInput(_ device: AnyObject) async throws {
    hid = try openConnection(device, service: Self.inputService)
    try await InputReadiness.check { try await self.inputBarrier(recordMetrics: false) }
  }

  func openConnection(_ device: AnyObject, service: String) throws -> xpc_connection_t {
    typealias Lookup =
      @convention(c) (AnyObject, Selector, NSString, UnsafeMutablePointer<NSError?>?) -> UInt32
    typealias Endpoint = @convention(c) (UInt32, UInt64, UInt64) -> Unmanaged<AnyObject>?
    typealias Enable = @convention(c) (xpc_connection_t) -> Void
    let create = try runtime.function("xpc_endpoint_create_mach_port_4sim", as: Endpoint.self)
    let enable = try runtime.function("xpc_connection_enable_sim2host_4sim", as: Enable.self)
    let lookup = try runtime.method(
      device, "lookup:error:", types: ["I", "@", ":", "@", "^@"], as: Lookup.self)
    var error: NSError?
    let port = lookup(
      device, NSSelectorFromString("lookup:error:"), service as NSString, &error)
    guard port != 0, port != UInt32.max else {
      throw BridgeError.unavailable(
        error?.localizedDescription ?? "The Simulator input service is unavailable")
    }
    // The private endpoint factory consumes this send right; the public connection owns its own right.
    guard let owned = create(port, 0, 0) else {
      mach_port_deallocate(mach_task_self_, port)
      throw BridgeError.unavailable("Cannot open the Simulator input endpoint")
    }
    guard let endpoint = owned.takeRetainedValue() as? xpc_endpoint_t,
      xpc_get_type(endpoint) == XPC_TYPE_ENDPOINT
    else {
      throw BridgeError.unavailable("The Simulator input endpoint has an unsupported type")
    }
    let connection = xpc_connection_create_from_endpoint(endpoint)
    enable(connection)
    let failures = inputFailure
    xpc_connection_set_target_queue(
      connection, DispatchQueue(label: "simulator-input", qos: .userInteractive))
    xpc_connection_set_event_handler(connection) { event in
      if xpc_get_type(event) == XPC_TYPE_ERROR {
        failures.withLock { $0 = "The Simulator input connection ended. Reconnect its picture." }
      }
    }
    xpc_connection_activate(connection)
    return connection
  }

  // A reply establishes daemon liveness and ordered transport progress, not application handling.
  func inputBarrier(recordMetrics: Bool = true) async throws {
    guard let hid else { throw BridgeError.unavailable("Simulator input is disconnected") }
    let started = DispatchTime.now().uptimeNanoseconds
    let metrics = recordMetrics ? self.metrics : nil
    let commandID = activeCommandID
    if let commandID { metrics?.inputTrace.mark(id: commandID, phase: .barrier, at: started) }
    defer { metrics?.record("barrierAwaitNs", DispatchTime.now().uptimeNanoseconds - started) }
    let (replies, continuation) = AsyncStream<Bool>.makeStream(bufferingPolicy: .bufferingNewest(1))
    xpc_connection_send_message_with_reply(
      hid, Self.message("IndigoKeyboardButtonEvent", barrier: true),
      DispatchQueue.global(qos: .userInteractive)
    ) { reply in
      let callback = DispatchTime.now().uptimeNanoseconds
      metrics?.record("barrierCallbackNs", callback - started)
      if let commandID { metrics?.inputTrace.mark(id: commandID, phase: .callback, at: callback) }
      continuation.yield(xpc_get_type(reply) != XPC_TYPE_ERROR)
      continuation.finish()
    }
    try await withThrowingTaskGroup(of: Void.self) { group in
      group.addTask {
        for await success in replies {
          guard success else {
            throw BridgeError.unavailable("The Simulator input service refused the connection")
          }
          return
        }
        throw CancellationError()
      }
      group.addTask {
        try await Task.sleep(for: .seconds(3))
        throw BridgeError.unavailable("Simulator input timed out; delivery is unknown")
      }
      defer {
        group.cancelAll()
        continuation.finish()
      }
      try await group.next()
    }
  }
}
