package app.anicanon.kumikae.as3.testing

import com.intellij.execution.filters.Filter
import com.intellij.execution.filters.HyperlinkInfo
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.TextRange
import com.intellij.openapi.vfs.LocalFileSystem
import java.nio.file.Path

internal data class As3FailureFrame(
    val path: String,
    val line: Int,
    val start: Int,
    val end: Int,
    val byteColumn: Int?,
    val callable: String?,
) {
    val framework get() = path.replace('\\', '/').contains("/app/anicanon/kumikae/testing/")

    fun descriptor(project: Project, root: Path): OpenFileDescriptor? {
        val local = path.removePrefix("res://")
        val absolute = runCatching {
            val source = Path.of(local)
            if (source.isAbsolute) source else root.resolve(source)
        }.getOrNull() ?: return null
        val file = LocalFileSystem.getInstance().findFileByNioFile(absolute.normalize()) ?: return null
        val row = line - 1
        if (byteColumn == null) return OpenFileDescriptor(project, file, row, 0)
        val document = FileDocumentManager.getInstance().getDocument(file) ?: return null
        if (row >= document.lineCount) return null
        val startOffset = document.getLineStartOffset(row)
        val text = document.getText(TextRange(startOffset, document.getLineEndOffset(row)))
        // kas3c columns count UTF-8 bytes. Rider navigates by UTF-16 document offsets.
        val bytes = text.toByteArray(Charsets.UTF_8)
        var length = (byteColumn - 1).coerceAtMost(bytes.size)
        while (length in 1 until bytes.size && bytes[length].toInt() and 0xc0 == 0x80) length--
        return OpenFileDescriptor(project, file, startOffset + bytes.decodeToString(0, length).length)
    }
}

internal object As3FailureFrames {
    private val stack = Regex("""(?m)^\h*at (?<call>[^\r\n]+)\(\)\[(?<path>[^\r\n]+):(?<line>\d+)\]\h*\r?$""")
    private val compiler = Regex("""(?m)^\h*(?<path>[^\r\n]+?):(?<line>\d+):(?<column>\d+): error:""")
    private val engine = Regex("""(?m)^\h*(?:at:|\[\d+\]) [^\r\n]*?\((?<path>res://[^\r\n]+):(?<line>\d+)\)\h*\r?$""")
    private val patterns = listOf(stack, compiler, engine)

    fun read(text: String): List<As3FailureFrame> = patterns.flatMap { pattern ->
        pattern.findAll(text).mapNotNull { match ->
            val path = checkNotNull(match.groups["path"])
            val row = checkNotNull(match.groups["line"])
            val line = row.value.toIntOrNull()?.takeIf { it > 0 } ?: return@mapNotNull null
            val column = if (pattern == compiler) match.groups["column"] else null
            val callable = if (pattern == stack) match.groups["call"]?.value else null
            val byteColumn = column?.let {
                it.value.toIntOrNull()?.takeIf { value -> value > 0 } ?: return@mapNotNull null
            }
            As3FailureFrame(path.value, line, path.range.first, (column ?: row).range.last + 1, byteColumn, callable)
        }
    }.sortedBy { it.start }
}

internal class As3FailureFilter(private val project: Project, private val root: Path) : Filter {
    override fun applyFilter(line: String, entireLength: Int): Filter.Result? {
        val frame = As3FailureFrames.read(line).firstOrNull() ?: return null
        val descriptor = frame.descriptor(project, root) ?: return null
        val base = entireLength - line.length
        return Filter.Result(base + frame.start, base + frame.end, HyperlinkInfo { descriptor.navigate(true) })
    }
}
