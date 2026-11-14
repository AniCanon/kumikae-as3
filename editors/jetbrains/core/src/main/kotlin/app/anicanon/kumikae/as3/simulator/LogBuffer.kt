package app.anicanon.kumikae.as3.simulator

data class LogEntry(val cursor: Long, val stream: String, val text: String)
data class LogPage(val entries: List<LogEntry>, val nextCursor: Long, val truncated: Boolean)

class LogBuffer(private val maxCharacters: Int = 256 * 1024) {
    init { require(maxCharacters > 0) }
    private val entries = ArrayDeque<LogEntry>()
    private var characters = 0
    private var cursor = 0L
    private var lostThrough = 0L

    @Synchronized fun append(stream: String, text: String) {
        if (text.isEmpty()) return
        val value = text.takeLast(maxCharacters)
        cursor++
        if (value.length < text.length) lostThrough = cursor
        entries.addLast(LogEntry(cursor, stream, value))
        characters += value.length
        while (characters > maxCharacters) {
            val old = entries.removeFirst()
            characters -= old.text.length
            lostThrough = maxOf(lostThrough, old.cursor)
        }
    }

    @Synchronized fun read(after: Long): LogPage {
        require(after in 0..cursor) { "Log cursor is outside this session" }
        return LogPage(entries.filter { it.cursor > after }, cursor, after < lostThrough)
    }
}
