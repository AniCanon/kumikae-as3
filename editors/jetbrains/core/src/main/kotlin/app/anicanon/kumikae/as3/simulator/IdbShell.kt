package app.anicanon.kumikae.as3.simulator

/** idb's own shell keeps one gRPC client warm; a failed command is never replayed. */
class IdbShell(private val child: StreamProcess) : AutoCloseable {
    private val replies = ProcessLines(child)

    @Volatile var closed = false
        private set
    val available: Boolean get() = !closed && child.alive

    @Synchronized fun describe(): String = execute("describe --json").single()

    @Synchronized fun send(operation: String, args: List<String>) {
        require(operation in setOf("tap", "swipe", "text", "button", "rotate")) { "Unknown input operation" }
        val separator = if (operation == "text") " --" else ""
        execute("ui $operation$separator " + args.joinToString(" ", transform = ::quote))
    }

    private fun execute(command: String): List<String> {
        if (closed) throw SimulatorException("INPUT_UNAVAILABLE", "idb input connection is closed")
        try {
            child.input.write("$command\n".toByteArray(Charsets.UTF_8))
            child.input.flush()
            val result = mutableListOf<String>()
            while (true) {
                when (val line = replies.next()) {
                    "SUCCESS=1" -> return result

                    "SUCCESS=0" -> throw SimulatorException("PROCESS_FAILED", "idb input failed. ${child.errors}")

                    else -> {
                        check(result.size < 8) { "Unexpected idb reply" }
                        result += line
                    }
                }
            }
        } catch (failure: Exception) {
            close()
            throw if (failure is SimulatorException) {
                failure
            } else {
                SimulatorException("INPUT_UNAVAILABLE", "idb input failed; completion is unknown: ${failure.message}")
            }
        }
    }

    override fun close() {
        closed = true
        replies.close()
    }

    private fun quote(value: String): String {
        require('\n' !in value && '\r' !in value && '\u0000' !in value) { "Invalid idb argument" }
        return "'" + value.replace("'", "'\"'\"'") + "'"
    }
}
