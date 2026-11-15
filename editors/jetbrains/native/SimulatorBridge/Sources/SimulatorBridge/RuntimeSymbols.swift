import Darwin
import Foundation
import ObjectiveC

// This contains only public runtime operations. Private names and ABI expectations belong to the adapter.
final class RuntimeSymbols: SymbolLookup {
  private let guardAPI: PrivateAPIGuard
  private var libraries: [UnsafeMutableRawPointer] = []
  private let message: UnsafeMutableRawPointer

  init(guardAPI: PrivateAPIGuard) throws {
    self.guardAPI = guardAPI
    guard let process = dlopen(nil, RTLD_NOW), let message = dlsym(process, "objc_msgSend") else {
      throw BridgeError.unavailable("The Objective-C runtime is unavailable")
    }
    self.message = message
  }

  func load(_ path: String) throws {
    guard let library = dlopen(path, RTLD_NOW | RTLD_LOCAL) else {
      throw BridgeError.unavailable(
        "\(guardAPI.version) (\(guardAPI.build)) cannot load \(path): \(dlerror().map { String(cString: $0) } ?? "unknown loader error")"
      )
    }
    // Frameworks own asynchronous runtime registrations. Keep them loaded until process exit.
    libraries.append(library)
  }

  func contains(_ symbol: String) -> Bool { libraries.contains { dlsym($0, symbol) != nil } }

  func function<T>(_ name: String, as type: T.Type) throws -> T {
    let pointer = libraries.lazy.compactMap { dlsym($0, name) }.first
    try guardAPI.require(pointer != nil, symbol: name)
    guard let pointer else { throw BridgeError.unavailable("Missing function \(name)") }
    return unsafeBitCast(pointer, to: type)
  }

  func type(_ name: String) throws -> AnyClass {
    let type: AnyClass? = NSClassFromString(name)
    try guardAPI.require(type != nil, symbol: name)
    guard let type else { throw BridgeError.unavailable("Missing class \(name)") }
    return type
  }

  func method<T>(_ object: AnyObject, _ name: String, types: [String], as type: T.Type) throws -> T
  {
    let selector = NSSelectorFromString(name)
    try guardAPI.require(
      (object as? NSObjectProtocol)?.responds(to: selector) == true, symbol: name)
    typealias Signature = @convention(c) (AnyObject, Selector, Selector) -> Unmanaged<AnyObject>?
    let signatureCall = unsafeBitCast(message, to: Signature.self)
    guard
      let signature = signatureCall(
        object, NSSelectorFromString("methodSignatureForSelector:"), selector)?
        .takeUnretainedValue()
    else {
      throw BridgeError.unavailable("Cannot inspect the signature of \(name)")
    }
    typealias Count = @convention(c) (AnyObject, Selector) -> UInt
    typealias Return = @convention(c) (AnyObject, Selector) -> UnsafePointer<CChar>
    typealias Argument = @convention(c) (AnyObject, Selector, UInt) -> UnsafePointer<CChar>
    let count = unsafeBitCast(message, to: Count.self)(
      signature, NSSelectorFromString("numberOfArguments"))
    let result = unsafeBitCast(message, to: Return.self)(
      signature, NSSelectorFromString("methodReturnType"))
    let argument = unsafeBitCast(message, to: Argument.self)
    var actual = [String(cString: result)]
    for index in 0..<count {
      actual.append(
        String(cString: argument(signature, NSSelectorFromString("getArgumentTypeAtIndex:"), index))
      )
    }
    // Opaque structure pointers are never dereferenced by this bridge; only their pointer ABI matters.
    let opaque = actual.map { value in
      value.hasPrefix("^{") ? String(value.prefix { $0 != "=" && $0 != "}" }) + "}" : value
    }
    try guardAPI.require(
      opaque == types, symbol: "\(name) signature \(opaque.joined(separator: ","))")
    return unsafeBitCast(message, to: type)
  }

  func object(_ receiver: AnyObject, _ name: String) throws -> AnyObject? {
    typealias Getter = @convention(c) (AnyObject, Selector) -> Unmanaged<AnyObject>?
    return try method(receiver, name, types: ["@", "@", ":"], as: Getter.self)(
      receiver, NSSelectorFromString(name))?.takeUnretainedValue()
  }

  func integer(_ receiver: AnyObject, _ name: String, encoding: String) throws -> UInt64 {
    typealias Getter = @convention(c) (AnyObject, Selector) -> UInt64
    return try method(receiver, name, types: [encoding, "@", ":"], as: Getter.self)(
      receiver, NSSelectorFromString(name))
  }
}
