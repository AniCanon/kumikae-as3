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
    val framework: Boolean
        get() {
            val normalized = path.replace('\\', '/')
            return normalized.startsWith(FRAMEWORK_PREFIX) || normalized.contains("/$FRAMEWORK_PREFIX")
        }

    fun descriptor(project: Project, root: Path): OpenFileDescriptor? {
        val file = resolveFile(project, root) ?: return null
        val row = (line - 1).coerceAtLeast(0)
        if (byteColumn == null) return OpenFileDescriptor(project, file, row, 0)
        val document = FileDocumentManager.getInstance().getDocument(file)
            ?: return OpenFileDescriptor(project, file, row, 0)
        if (row >= document.lineCount) return OpenFileDescriptor(project, file, row, 0)
        val startOffset = document.getLineStartOffset(row)
        val text = document.getText(TextRange(startOffset, document.getLineEndOffset(row)))
        return OpenFileDescriptor(project, file, startOffset + As3FailureFrames.utf16Column(text, byteColumn))
    }

    private fun resolveFile(project: Project, root: Path) = if (path.startsWith("res://")) {
        val relative = path.removePrefix("res://").trimStart('/')
        val roots = buildList {
            add(root)
            project.basePath?.takeIf { it.isNotEmpty() }?.let { Path.of(it) }
                ?.takeIf { it.normalize() != root.normalize() }
                ?.let { add(it) }
        }
        roots.firstNotNullOfOrNull { base ->
            val normalizedBase = base.toAbsolutePath().normalize()
            val candidate = runCatching { normalizedBase.resolve(relative).normalize() }.getOrNull()
                ?.takeIf { it.startsWith(normalizedBase) }
                ?: return@firstNotNullOfOrNull null
            LocalFileSystem.getInstance().findFileByNioFile(candidate)
        }
    } else {
        val absolute = runCatching {
            val source = Path.of(path)
            if (source.isAbsolute) source else root.resolve(source)
        }.getOrNull()
        absolute?.let { LocalFileSystem.getInstance().findFileByNioFile(it.normalize()) }
    }

    private companion object {
        const val FRAMEWORK_PREFIX = "app/anicanon/kumikae/testing/"
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

    fun preferredOrder(frames: List<As3FailureFrame>, testCallable: String?): List<As3FailureFrame> {
        val nonFramework = frames.filterNot { it.framework }
        if (frames.firstOrNull()?.framework != true || testCallable.isNullOrEmpty()) return nonFramework
        val prefix = "$testCallable/"
        val inTest = nonFramework.filter { it.callable == testCallable || it.callable?.startsWith(prefix) == true }
        return if (inTest.isEmpty()) nonFramework else inTest + nonFramework.filterNot { it in inTest }
    }

    // kas3c columns count 1-based UTF-8 bytes. Rider navigates by 0-based UTF-16 document offsets.
    fun utf16Column(lineText: String, byteColumn: Int): Int {
        val bytes = lineText.toByteArray(Charsets.UTF_8)
        var length = (byteColumn - 1).coerceIn(0, bytes.size)
        while (length in 1 until bytes.size && bytes[length].toInt() and 0xc0 == 0x80) length--
        return bytes.decodeToString(0, length).length
    }
}

internal class As3FailureFilter(private val project: Project, private val root: Path) : Filter {
    override fun applyFilter(line: String, entireLength: Int): Filter.Result? {
        val frame = As3FailureFrames.read(line).firstOrNull() ?: return null
        val descriptor = frame.descriptor(project, root) ?: return null
        val base = entireLength - line.length
        return Filter.Result(base + frame.start, base + frame.end, HyperlinkInfo { descriptor.navigate(true) })
    }
}
