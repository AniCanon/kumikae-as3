package app.anicanon.kumikae.as3.settings

import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ModalityState
import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.progress.ProgressManager
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.vfs.VFileProperty
import com.intellij.openapi.vfs.VfsUtilCore
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.util.concurrency.AppExecutorUtil
import javax.imageio.ImageIO

internal data class ArtworkCandidate(val art: Artwork?, val selectable: Boolean)

internal class ProjectArtworkLibrary(private val root: VirtualFile, private val owner: Disposable) {
    @Volatile var external: VirtualFile? = null
        private set
    private fun resolve(path: String): VirtualFile? = external?.takeIf { it.url == path && it.isValid }
        ?: path.takeIf { it.startsWith("res://") }?.let { root.findFileByRelativePath(it.removePrefix("res://")) }

    fun stage(file: VirtualFile): String {
        val relative = VfsUtilCore.getRelativePath(file, root)
        val inGame = relative != null && generateSequence(file) { it.parent }.takeWhile { it != root }.all(::included)
        if (inGame) return "res://$relative"
        external = file
        return file.url
    }

    fun importFile(path: String): String {
        val source = requireNotNull(external?.takeIf { it.isValid && it.url == path }) { "The chosen image moved." }
        val result = ProjectImageImport.copy(source.toNioPath(), root.toNioPath())
        val target = root.toNioPath().resolve(result.removePrefix("res://"))
        LocalFileSystem.getInstance().refreshAndFindFileByNioFile(target)
        return result
    }

    private fun included(entry: VirtualFile): Boolean {
        if (entry.name.startsWith('.') || entry.`is`(VFileProperty.SYMLINK)) return false
        return !entry.isDirectory || (entry.findChild(".gdignore") == null && entry.findChild("project.godot") == null)
    }

    fun list(kind: GameResourceKind, current: String, done: (ArtworkChoices) -> Unit) = read("catalog", {
        val paths = mutableListOf<String>()
        VfsUtilCore.iterateChildrenRecursively(root, { entry ->
            entry == root || included(entry)
        }) { entry ->
            ProgressManager.checkCanceled()
            if (!entry.isDirectory && kind.accepts(entry.name)) {
                VfsUtilCore.getRelativePath(entry, root)?.let { paths.add("res://$it") }
            }
            true
        }
        ArtworkChoices(paths, current)
    }, done)

    fun thumbnails(paths: List<String>, done: (Map<String, Artwork?>) -> Unit) = read("thumbnails", {
        paths.associateWith { path ->
            ProgressManager.checkCanceled()
            resolve(path)?.let { GameArtwork.decode(it, 180, true) }
        }
    }, done)

    fun preview(path: String, filter: Boolean, done: (ArtworkCandidate) -> Unit) = read("selection", {
        val file = resolve(path)
        val art = file?.let { GameArtwork.decode(it, 960, filter) }
        val lacksReader = file != null && file.length > 0 && !file.extension.equals("svg", true) &&
            !ImageIO.getImageReadersBySuffix(file.extension).hasNext()
        ArtworkCandidate(art, art != null || lacksReader)
    }, done)

    private fun <T> read(job: String, action: () -> T, done: (T) -> Unit) {
        ReadAction.nonBlocking<T> { action() }.expireWith(owner).coalesceBy(this, job)
            .finishOnUiThread(ModalityState.any(), done).submit(AppExecutorUtil.getAppExecutorService())
    }
}
