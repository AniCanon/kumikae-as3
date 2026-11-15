struct CommandReply: Encodable, Sendable {
  let id: Int
  let receivedNanos: UInt64
  let completedNanos: UInt64
  let ok: Bool
  let error: String?

  init(id: Int, receivedNanos: UInt64, completedNanos: UInt64, error: String?) {
    self.id = id
    self.receivedNanos = receivedNanos
    self.completedNanos = completedNanos
    self.ok = error == nil
    self.error = error
  }
}
