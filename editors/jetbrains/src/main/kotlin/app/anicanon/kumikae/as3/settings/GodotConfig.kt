package app.anicanon.kumikae.as3.settings

internal data class ConfigKey(val section: String, val name: String)

/** A surgical editor: values outside the requested keys are never reserialized. */
internal class GodotConfig(private val source: String) {
    private data class Entry(val start: Int, val end: Int, val value: String)
    private val entries = linkedMapOf<ConfigKey, Entry>()
    private val sectionEnds = linkedMapOf<String, Int>()
    val sections: List<String> get() = sectionEnds.keys.toList()

    init {
        var section = ""
        var offset = 0
        for (line in source.splitToSequence('\n')) {
            val clean = line.trim()
            if (clean.startsWith("[")) {
                require(clean.endsWith("]")) { "Malformed section: $clean" }
                sectionEnds[section] = offset
                section = clean.substring(1, clean.length - 1)
                require(section !in sectionEnds) { "Duplicate section: $section" }
            } else if (clean.isNotEmpty() && !clean.startsWith(";") && !clean.startsWith("#")) {
                val equals = line.indexOf('=')
                if (equals > 0) {
                    val name = line.substring(0, equals).trim()
                    val tail = line.substring(equals + 1)
                    val value = withoutComment(tail).trim()
                    val start = offset + equals + 1 + tail.indexOfFirst { !it.isWhitespace() }.coerceAtLeast(0)
                    val key = ConfigKey(section, name)
                    require(key !in entries) { "Duplicate setting: $section/$name" }
                    entries[key] = Entry(start, start + value.length, value)
                }
            }
            offset += line.length + 1
        }
        sectionEnds[section] = source.length
    }

    fun raw(section: String, name: String): String? = entries[ConfigKey(section, name)]?.value
    fun string(section: String, name: String, default: String = ""): String =
        raw(section, name)?.let(::unquote) ?: default

    fun update(values: Map<ConfigKey, String>): String {
        val edits = mutableListOf<Triple<Int, Int, String>>()
        val missing = linkedMapOf<String, MutableList<String>>()
        val newline = if (source.contains("\r\n")) "\r\n" else "\n"
        for ((key, value) in values) {
            require(!value.contains('\n') && !value.contains('\r')) { "A setting value must occupy one line" }
            val entry = entries[key]
            if (entry != null) {
                edits += Triple(entry.start, entry.end, value)
            } else {
                missing.getOrPut(key.section) { mutableListOf() }.add("${key.name}=$value")
            }
        }
        val append = StringBuilder()
        for ((section, lines) in missing) {
            val at = sectionEnds[section]
            val text = lines.joinToString(newline, postfix = newline)
            if (at != null) {
                val prefix = if (at > 0 && source[at - 1] != '\n') newline else ""
                edits += Triple(at, at, prefix + text)
            } else {
                append.append(newline).append("[$section]").append(newline).append(text)
            }
        }
        var result = source
        for ((start, end, text) in edits.sortedByDescending { it.first }) result = result.replaceRange(start, end, text)
        return result + append
    }

    companion object {
        fun quote(value: String): String = "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"")
            .replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t") + "\""

        fun unquote(value: String): String {
            require(value.length >= 2 && value.first() == '"' && value.last() == '"') {
                "Unsupported value; edit it in Source: $value"
            }
            val result = StringBuilder()
            var escaped = false
            for (char in value.substring(1, value.length - 1)) {
                if (escaped) {
                    result.append(
                        when (char) {
                            'n' -> '\n'
                            'r' -> '\r'
                            't' -> '\t'
                            '\\', '"' -> char
                            else -> error("Unsupported string escape: \\$char")
                        },
                    )
                    escaped = false
                } else if (char == '\\') {
                    escaped = true
                } else {
                    require(char != '"') { "Malformed quoted value" }
                    result.append(char)
                }
            }
            require(!escaped) { "Unterminated string escape" }
            return result.toString()
        }

        private fun withoutComment(value: String): String {
            var quoted = false
            var escaped = false
            for ((index, char) in value.withIndex()) {
                if (escaped) {
                    escaped = false
                } else {
                    when (char) {
                        '\\' -> if (quoted) escaped = true
                        '"' -> quoted = !quoted
                        ';', '#' -> if (!quoted) return value.substring(0, index)
                    }
                }
            }
            return value
        }
    }
}
