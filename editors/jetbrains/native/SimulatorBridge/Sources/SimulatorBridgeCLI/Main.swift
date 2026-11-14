import Darwin
import Foundation
import SimulatorBridge

@main
struct Main {
  static func main() async {
    signal(SIGPIPE, SIG_IGN)
    do {
      let arguments = Array(CommandLine.arguments.dropFirst())
      let layout = try FrameLayout.load()
      if arguments == ["--version"] {
        print("SimulatorBridge 1; frame header \(layout.headerBytes) bytes")
        return
      }
      guard arguments.count == 4, arguments[0] == "--device", UUID(uuidString: arguments[1]) != nil,
        arguments[2] == "--frames", arguments[3].hasPrefix("/")
      else {
        throw BridgeError.invalid(
          "Usage: simulator-bridge --device <id> --frames <absolute-new-file>")
      }
      let commands = try CommandChannel()
      let source = try AppleSimulator(installation: XcodeInstallation.current(), layout: layout)
      let sink = try MappedFrameSink(path: arguments[3], layout: layout)
      do {
        let device = try await source.connect(device: arguments[1])
        try await FrameStartup.publish(from: source, to: sink)
        try CommandChannel.write([
          "ready": true, "width": device.width, "height": device.height, "density": device.density,
        ])
      } catch {
        await source.disconnect()
        try await sink.close()
        throw error
      }
      try await BridgeRunner(
        device: source, source: source, sink: sink, input: source, commands: commands
      ).run()
    } catch {
      let message = "Native Simulator unavailable: \(error.localizedDescription)"
      do {
        try CommandChannel.write([
          "error": message, "code": (error as? BridgeError)?.code ?? "INPUT_UNAVAILABLE",
        ])
      } catch {
        FileHandle.standardError.write(Data("\(message)\n".utf8))
      }
      exit(EXIT_FAILURE)
    }
  }
}
