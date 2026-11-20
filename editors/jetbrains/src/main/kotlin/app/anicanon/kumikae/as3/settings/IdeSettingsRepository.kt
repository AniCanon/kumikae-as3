package app.anicanon.kumikae.as3.settings

import app.anicanon.kumikae.as3.panel.BackgroundTasks
import app.anicanon.kumikae.as3.scene.resolveProjectResource
import app.anicanon.kumikae.as3.simulator.ProcessRunner
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ModalityState
import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.editor.Document
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.SystemInfo
import com.intellij.openapi.vfs.ReadonlyStatusHandler
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.util.concurrency.AppExecutorUtil
import java.nio.file.Path

/** Uses IDE documents so Apply participates in undo and sees unsaved source edits. */
internal class IdeSettingsRepository(
    private val project: Project,
    private val file: VirtualFile,
    private val owner: Disposable,
    private val tasks: BackgroundTasks,
) : SettingsRepository {
    override val hasXcodeTeams get() = SystemInfo.isMac
    override fun loadTeams(ready: (Result<List<SigningTeam>>) -> Unit) {
        tasks.submit({
            val preferences = Path.of(System.getProperty("user.home"), "Library/Preferences/com.apple.dt.Xcode.plist")
            XcodeTeams.load(ProcessRunner(), preferences)
        }, ready)
    }
    override fun openXcode(ready: (Result<Unit>) -> Unit) {
        tasks.submit({
            ProcessRunner().run(listOf("/usr/bin/open", "-b", "com.apple.dt.Xcode"), 5000)
            Unit
        }, ready)
    }

    private fun root(): VirtualFile = requireNotNull(file.parent) { "Project file has no parent directory." }

    private fun documents(): Pair<Document, Document?> =
        ReadAction.computeBlocking<Pair<Document, Document?>, RuntimeException> {
            val manager = FileDocumentManager.getInstance()
            val main = requireNotNull(manager.getDocument(file)) { "Cannot open project.godot." }
            val exports = root().findChild("export_presets.cfg")?.let(manager::getDocument)
            main to exports
        }

    override fun loadPreview(path: String, filter: Boolean, ready: (Artwork?) -> Unit) {
        ReadAction.nonBlocking<Artwork?> {
            file.parent?.let { GameArtwork.preview(it, path, filter) }
        }.expireWith(owner).finishOnUiThread(ModalityState.any(), ready)
            .submit(AppExecutorUtil.getAppExecutorService())
    }

    override fun load(): ProjectSettings {
        val (main, exports) = documents()
        return ProjectSettings.read(main.text, exports?.text.orEmpty())
    }

    override fun save(expected: ProjectSettings, draft: ProjectSettings): ProjectSettings {
        val (main, exports) = documents()
        check(main.text == expected.projectText && exports?.text.orEmpty() == expected.exportText) {
            "Project files changed. Reload before applying your changes."
        }
        val (newMain, newExports) = draft.write(main.text, exports?.text.orEmpty())
        val folder = root()
        val resources = listOf(draft.mainScene, draft.icon, draft.splash) +
            draft.allExportDrafts().values.map { it.icon }
        for (resource in resources) {
            if (resource.startsWith("res://")) {
                require(resolveProjectResource(folder, resource) != null) {
                    "Resource does not exist: $resource"
                }
            }
        }
        val files = listOfNotNull(file, folder.findChild("export_presets.cfg"))
        check(!ReadonlyStatusHandler.getInstance(project).ensureFilesWritable(files).hasReadonlyFiles()) {
            "Project settings are read-only."
        }
        WriteCommandAction.runWriteCommandAction(
            project,
            "Apply Godot project settings",
            null,
            Runnable {
                check(main.text == expected.projectText && exports?.text.orEmpty() == expected.exportText) {
                    "Project files changed. Reload before applying your changes."
                }
                try {
                    main.setText(newMain)
                    exports?.setText(newExports)
                } catch (error: Exception) {
                    runCatching { main.setText(expected.projectText) }.onFailure(error::addSuppressed)
                    runCatching { exports?.setText(expected.exportText) }.onFailure(error::addSuppressed)
                    throw error
                }
            },
        )
        return load().select(draft.preset)
    }
}
