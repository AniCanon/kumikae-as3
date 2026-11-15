/// Wire names stay at the boundary; the adapter dispatches exhaustively over known operations.
enum InputOperation: String, Sendable {
  case ping, down, move, up, home, lock, rotate, paste, text
}

extension InputCommand {
  var operation: InputOperation {
    get throws {
      guard let operation = InputOperation(rawValue: op) else {
        throw BridgeError.invalid("Unsupported native input command: \(op)")
      }
      return operation
    }
  }
}
