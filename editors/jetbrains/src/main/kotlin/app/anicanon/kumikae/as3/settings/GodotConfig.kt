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
        for ((offset, line) in records(source)) {
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
                    val value = tail.trim()
                    val firstNonBlank = tail.indexOfFirst { !it.isWhitespace() }
                    val start = offset + equals + 1 + if (firstNonBlank >= 0) firstNonBlank else 0
                    val end = if (firstNonBlank >= 0) start + value.length else offset + line.length
                    val key = ConfigKey(section, name)
                    require(key !in entries) { "Duplicate setting: $section/$name" }
                    entries[key] = Entry(start, end, value)
                }
            }
        }
        sectionEnds[section] = source.length
    }

    fun properties(section: String): List<String> = entries.keys.filter { it.section == section }.map { it.name }

    fun raw(section: String, name: String): String? = entries[ConfigKey(section, name)]?.value
    fun string(section: String, name: String, default: String = ""): String =
        raw(section, name)?.let(::unquote) ?: default

    private data class Edit(val start: Int, val end: Int, val text: String, val isAppend: Boolean)

    fun update(values: Map<ConfigKey, String>): String {
        val edits = mutableListOf<Edit>()
        val missing = linkedMapOf<String, MutableList<String>>()
        val newline = if (source.contains("\r\n")) "\r\n" else "\n"
        for ((key, value) in values) {
            require(!value.contains('\n') && !value.contains('\r')) { "A setting value must occupy one line" }
            val entry = entries[key]
            if (entry != null) {
                edits += Edit(entry.start, entry.end, value, isAppend = false)
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
                edits += Edit(at, at, prefix + text, isAppend = true)
            } else {
                append.append(newline).append("[$section]").append(newline).append(text)
            }
        }
        var result = source
        val ordered = edits.sortedWith(compareByDescending<Edit> { it.start }.thenByDescending { it.isAppend })
        for (edit in ordered) result = result.replaceRange(edit.start, edit.end, edit.text)
        return result + append
    }

    companion object {
        fun quote(value: String): String = "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"")
            .replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t") + "\""

        fun unquote(value: String): String {
            require(value.length >= 2 && value.first() == '"' && value.last() == '"') {
                "Unsupported value; edit it in Source: $value"
            }
            val body = value.substring(1, value.length - 1)
            val result = StringBuilder(body.length)
            var index = 0
            while (index < body.length) {
                val char = body[index++]
                if (char == '\\') {
                    require(index < body.length) { "Unterminated string escape" }
                    when (val escaped = body[index++]) {
                        'n' -> result.append('\n')

                        'r' -> result.append('\r')

                        't' -> result.append('\t')

                        'b' -> result.append('\b')

                        'f' -> result.append('\u000C')

                        '\\', '"', '\'' -> result.append(escaped)

                        'u', 'U' -> {
                            val digits = if (escaped == 'u') 4 else 8
                            require(index + digits <= body.length) { "Unterminated unicode escape" }
                            val codePoint = requireNotNull(body.substring(index, index + digits).toIntOrNull(16)) {
                                "Invalid unicode escape"
                            }
                            require(Character.isValidCodePoint(codePoint)) { "Invalid unicode code point" }
                            result.appendCodePoint(codePoint)
                            index += digits
                        }

                        else -> throw IllegalArgumentException("Unsupported string escape: \\$escaped")
                    }
                } else {
                    require(char != '"') { "Malformed quoted value" }
                    result.append(char)
                }
            }
            return result.toString()
        }

        private fun records(source: String): Sequence<Pair<Int, String>> = sequence {
            var start = 0
            var end = 0
            var depth = 0
            var quoted = false
            var escaped = false
            var comment = false
            for ((offset, char) in source.withIndex()) {
                if (char == '\n') {
                    comment = false
                    if (!quoted && depth == 0) {
                        yield(start to source.substring(start, end))
                        start = offset + 1
                        end = start
                    }
                    escaped = false
                    continue
                }
                if (comment) continue
                when {
                    escaped -> escaped = false
                    quoted && char == '\\' -> escaped = true
                    char == '"' -> quoted = !quoted
                    !quoted && (char == ';' || char == '#') -> comment = true
                    !quoted && char in "[({" -> depth++
                    !quoted && char in "])}" -> depth--
                }
                require(depth >= 0) { "Unexpected closing delimiter in a Godot setting." }
                if (!comment) end = offset + 1
            }
            require(!quoted && depth == 0) { "Unterminated Godot setting. Check strings and brackets in Source." }
            if (start < source.length) yield(start to source.substring(start, end))
        }
    }
}
