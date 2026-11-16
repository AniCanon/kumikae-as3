package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.application.runReadAction
import com.intellij.openapi.application.runWriteAction
import com.intellij.openapi.project.Project
import com.intellij.openapi.project.RootsChangeRescanningInfo
import com.intellij.openapi.roots.ProjectFileIndex
import com.intellij.openapi.roots.ex.ProjectRootManagerEx
import com.intellij.openapi.util.EmptyRunnable
import com.intellij.openapi.vfs.LocalFileSystem
import java.nio.file.Files
import java.nio.file.Path

/**
 * A game's build folders are not indexed: a staged copy of a script under `.build` is excluded, so search
 * and navigation never offer it, while the game's own script beside the project file is not.
 */
internal object UiProbeBuildFolders {
    fun run(project: Project, folder: Path, observations: MutableList<String>) {
        Files.writeString(folder.resolve("kas3.gdextension"), "[configuration]\n")
        val copy = folder.resolve(".build/project/Staged.as")
        Files.createDirectories(copy.parent)
        Files.writeString(copy, "package { public class Staged {} }\n")
        val own = folder.resolve("Own.as")
        Files.writeString(own, "package { public class Own {} }\n")
        val files = LocalFileSystem.getInstance()
        val staged = checkNotNull(files.refreshAndFindFileByNioFile(copy)) { "The staged copy was not found" }
        val game = checkNotNull(files.refreshAndFindFileByNioFile(own)) { "The game's script was not found" }
        UiProbeCompletion.edt {
            runWriteAction {
                // The project became an AS3 game just now: what is excluded is asked again, as at its opening.
                ProjectRootManagerEx.getInstanceEx(project).makeRootsChange(
                    EmptyRunnable.getInstance(),
                    RootsChangeRescanningInfo.TOTAL_RESCAN,
                )
            }
        }
        UiProbeCompletion.await("build folders excluded") {
            runReadAction { ProjectFileIndex.getInstance(project).isExcluded(staged) }
        }
        check(runReadAction { !ProjectFileIndex.getInstance(project).isExcluded(game) }) {
            "The game's own script is excluded"
        }
        observations += "A staged copy under .build is excluded from the index; the game's own script is not"
    }
}
