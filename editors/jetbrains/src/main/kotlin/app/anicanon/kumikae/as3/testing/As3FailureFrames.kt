package app.anicanon.kumikae.as3.testing

import com.intellij.execution.filters.Filter
import com.intellij.execution.filters.HyperlinkInfo
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import java.nio.file.Path

internal data class As3FailureFrame(val path: String, val line: Int, val start: Int, val end: Int) {
    val framework get() = path.replace('\\', '/').contains("/kumikae/testing/")

    fun descriptor(project: Project, root: Path): OpenFileDescriptor? {
        val local = path.removePrefix("res://")
        val absolute = runCatching {
            val source = Path.of(local)
            if (source.isAbsolute) source else root.resolve(source)
        }.getOrNull() ?: return null
        val file = LocalFileSystem.getInstance().findFileByNioFile(absolute.normalize()) ?: return null
        return OpenFileDescriptor(project, file, line - 1, 0)
    }
}

internal object As3FailureFrames {
    private val frame = Regex("(?m)^\\s*at [^\\r\\n]+\\(\\)\\[([^\\r\\n]+):(\\d+)\\]\\s*$")
    fun read(text: String): List<As3FailureFrame> = frame.findAll(text).mapNotNull {
        val line = it.groupValues[2].toIntOrNull()?.takeIf { value -> value > 0 } ?: return@mapNotNull null
        val path = checkNotNull(it.groups[1])
        As3FailureFrame(path.value, line, path.range.first, checkNotNull(it.groups[2]).range.last + 1)
    }.toList()
}

internal class As3FailureFilter(private val project: Project, private val root: Path) : Filter {
    override fun applyFilter(line: String, entireLength: Int): Filter.Result? {
        val frame = As3FailureFrames.read(line).firstOrNull() ?: return null
        val descriptor = frame.descriptor(project, root) ?: return null
        val base = entireLength - line.length
        return Filter.Result(base + frame.start, base + frame.end, HyperlinkInfo { descriptor.navigate(true) })
    }
}
