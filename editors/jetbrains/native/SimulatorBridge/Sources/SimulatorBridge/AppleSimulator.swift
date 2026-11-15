import Darwin
import Foundation
import IOSurface
import ObjectiveC
import XPC
import os

/// The only type that knows private Apple symbols. No private framework is linked into the executable.
public actor AppleSimulator: DeviceLookup, FrameSource, InputSink {
  private let installation: XcodeInstallation
  let metrics: BridgeMetrics?
  let runtime: RuntimeSymbols
  private let layout: FrameLayout
  var device: AnyObject?
  var display: AnyObject?
  var hid: xpc_connection_t?
  var orientationConnection: xpc_connection_t?
  var rotationRequested = false
  let inputFailure = OSAllocatedUnfairLock<String?>(initialState: nil)
  private var used = false
  private var generation: UInt64 = 0
  private var sequence: UInt64 = 0
  private var pixelBuffer = PixelCopyBuffer()
  private var lastSurface: UInt32 = 0
  private var lastSeed: UInt32 = 0
  var frameRotation: FrameRotation?
  private var surfaceSize = (width: 0, height: 0)
  var info = DeviceInfo(width: 0, height: 0, density: 1)
  var finger: (Double, Double)?
  var activeCommandID: Int?
  var heldKeys: Set<UInt32> = []
  var heldButton: UInt32?
  private typealias ObjectError =
    @convention(c) (AnyObject, Selector, UnsafeMutablePointer<NSError?>?) -> Unmanaged<AnyObject>?

  public init(installation: XcodeInstallation, layout: FrameLayout, metrics: BridgeMetrics? = nil) throws {
    self.metrics = metrics
    self.installation = installation
    self.layout = layout
    let guardAPI = PrivateAPIGuard(
      version: installation.version, build: installation.build)
    runtime = try RuntimeSymbols(guardAPI: guardAPI)
    try guardAPI.validate([], using: runtime)
    #if !arch(arm64)
      throw BridgeError.unavailable(
        "The native Simulator bridge is currently validated only on Apple silicon.")
    #endif
    try runtime.load("/Library/Developer/PrivateFrameworks/CoreSimulator.framework/CoreSimulator")
    try runtime.load("/usr/lib/system/libxpc.dylib")
    try guardAPI.validate(
      [
        "xpc_endpoint_create_mach_port_4sim", "xpc_connection_enable_sim2host_4sim",
      ], using: runtime)
  }

  public func connect(device identifier: String) async throws -> DeviceInfo {
    guard !used else { throw BridgeError.invalid("Reconnect with a new Simulator helper process") }
    used = true
    let contextClass: AnyClass = try runtime.type("SimServiceContext")
    let contextName = "sharedServiceContextForDeveloperDir:error:"
    typealias Context =
      @convention(c) (AnyClass, Selector, NSString, UnsafeMutablePointer<NSError?>?) -> Unmanaged<
        AnyObject
      >?
    let contextCall = try runtime.method(
      contextClass, contextName, types: ["@", "@", ":", "@", "^@"], as: Context.self)
    var error: NSError?
    guard
      let context = contextCall(
        contextClass, NSSelectorFromString(contextName),
        installation.developerDirectory as NSString, &error)?.takeUnretainedValue()
    else {
      throw BridgeError.unavailable(
        error?.localizedDescription ?? "CoreSimulator could not connect")
    }
    let setName = "defaultDeviceSetWithError:"
    let setCall = try runtime.method(
      context, setName, types: ["@", "@", ":", "^@"], as: ObjectError.self)
    guard let set = setCall(context, NSSelectorFromString(setName), &error)?.takeUnretainedValue(),
      let devices = try runtime.object(set, "availableDevices") as? [AnyObject]
    else {
      throw BridgeError.unavailable(
        error?.localizedDescription ?? "CoreSimulator could not list devices")
    }
    for candidate in devices {
      guard let uuid = try runtime.object(candidate, "UDID") as? UUID, uuid.uuidString == identifier
      else { continue }
      device = candidate
    }
    guard let device, try runtime.integer(device, "state", encoding: "Q") == 3 else {
      throw BridgeError.deviceUnavailable(
        "The selected Simulator is not booted. Boot it, then reconnect.")
    }
    guard let io = try runtime.object(device, "io"),
      let ports = try runtime.object(io, "ioPorts") as? [AnyObject]
    else {
      throw BridgeError.unavailable(
        "Xcode \(installation.version) (\(installation.build)) has no Simulator ioPorts")
    }
    generation = try runtime.integer(device, "bootGeneration", encoding: "Q")
    for port in ports {
      guard let descriptor = try runtime.object(port, "descriptor"),
        (descriptor as? NSObjectProtocol)?.responds(to: NSSelectorFromString("screenProperties"))
          == true,
        let properties = try runtime.object(descriptor, "screenProperties"),
        try runtime.object(properties, "uniqueId") as? String == "PurpleMain"
      else { continue }
      display = descriptor
    }
    guard let display,
      let surface = try runtime.object(display, "framebufferSurface") as? IOSurface,
      let deviceType = try runtime.object(device, "deviceType")
    else {
      throw BridgeError.unavailable(
        "Xcode \(installation.version) (\(installation.build)) has no PurpleMain screenProperties/framebufferSurface. Reconnect after booting."
      )
    }
    typealias Family = @convention(c) (AnyObject, Selector) -> Int32
    let family = try runtime.method(
      deviceType, "productFamilyID", types: ["i", "@", ":"], as: Family.self)(
        deviceType, NSSelectorFromString("productFamilyID"))
    try NativeDeviceFamily.validate(family)
    typealias Scale = @convention(c) (AnyObject, Selector) -> Float
    let scale = try runtime.method(
      deviceType, "mainScreenScale", types: ["f", "@", ":"], as: Scale.self)(
        deviceType, NSSelectorFromString("mainScreenScale"))
    guard scale.isFinite, (1...10).contains(scale) else {
      throw BridgeError.invalid("Invalid Simulator screen density or identifier")
    }
    info = DeviceInfo(width: surface.width, height: surface.height, density: Double(scale))
    try await connectInput(device)
    return info
  }

  public func nextFrame() throws -> CapturedFrame? {
    // A rotation command revokes this stream; the service reconnects after its reply.
    if rotationRequested { return nil }
    if let failure = inputFailure.withLock({ $0 }) { throw BridgeError.unavailable(failure) }
    guard let device, let display, try runtime.integer(device, "state", encoding: "Q") == 3,
      try runtime.integer(device, "bootGeneration", encoding: "Q") == generation,
      let surface = try runtime.object(display, "framebufferSurface") as? IOSurface
    else {
      throw BridgeError.deviceUnavailable(
        "The Simulator stopped or restarted. Reconnect its picture.")
    }
    guard let properties = try runtime.object(display, "screenProperties") else {
      throw BridgeError.unavailable("Missing Simulator screenProperties")
    }
    let rotation = try FrameRotation(runtime.integer(properties, "uiOrientation", encoding: "I"))
    try rotation.checkContinuation(from: frameRotation)
    let identity = IOSurfaceGetID(surface)
    let seed = surface.seed
    if identity == lastSurface && seed == lastSeed && rotation.orientation == frameRotation?.orientation {
      metrics?.record("seedUnchanged")
      return nil
    }
    metrics?.record("seedChanged")
    let observed = DispatchTime.now().uptimeNanoseconds
    guard surface.pixelFormat == layout.pixelFormat, surface.width > 0, surface.height > 0,
      surface.width <= layout.maxDimension, surface.height <= layout.maxDimension,
      surface.bytesPerRow >= surface.width * 4, surface.bytesPerRow <= layout.maxDimension * 4,
      surface.allocationSize >= surface.bytesPerRow * surface.height
    else { throw BridgeError.invalid("Unsupported Simulator framebuffer format or dimensions") }
    guard surface.lock(options: .readOnly, seed: nil) == 0 else {
      throw BridgeError.unavailable("The Simulator framebuffer could not be locked")
    }
    let copyStart = DispatchTime.now().uptimeNanoseconds
    let pixels = pixelBuffer.copy(from: surface.baseAddress, count: surface.bytesPerRow * surface.height)
    metrics?.record("pixelCopyNs", DispatchTime.now().uptimeNanoseconds - copyStart)
    if pixelBuffer.storageChanged { metrics?.record("pixelStorageChanged") }
    guard surface.unlock(options: .readOnly, seed: nil) == 0 else {
      throw BridgeError.unavailable("The Simulator framebuffer could not be unlocked")
    }
    guard surface.seed == seed,
      let currentProperties = try runtime.object(display, "screenProperties"),
      try runtime.integer(currentProperties, "uiOrientation", encoding: "I") == rotation.orientation
    else { metrics?.record("changedDuringCopy"); return nil }
    metrics?.record("captured")
    lastSurface = identity
    lastSeed = seed
    sequence += 1
    if finger != nil && (rotation.orientation != frameRotation?.orientation ||
        surface.width != surfaceSize.width || surface.height != surfaceSize.height) {
      throw BridgeError.unavailable(
        "Screen geometry changed during a touch. Reconnect the picture.")
    }
    info = DeviceInfo(width: surface.width, height: surface.height, density: info.density)
    let header = FrameHeader(
      sequence: sequence, width: info.width, height: info.height, stride: surface.bytesPerRow,
      observedNanos: observed, writtenNanos: observed, nativeWidth: info.width,
      nativeHeight: info.height,
      densityMilli: Int(info.density * 1000))
    let frame = try rotation.apply(CapturedFrame(header: header, pixels: pixels), layout: layout)
    frameRotation = rotation
    surfaceSize = (surface.width, surface.height)
    info = DeviceInfo(width: frame.header.width, height: frame.header.height, density: info.density)
    return frame
  }

}
