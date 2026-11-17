package app.anicanon.kumikae.as3.settings

import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.ReadonlyStatusHandler
import com.intellij.openapi.vfs.VirtualFile

/** Uses IDE documents so Apply participates in undo and sees unsaved source edits. */
internal class IdeSettingsRepository(
    private val project: Project,
    private val file: VirtualFile,
    private val owner: com.intellij.openapi.Disposable,
) : SettingsRepository {
    private fun documents() = FileDocumentManager.getInstance().let { manager ->
        val main = requireNotNull(manager.getDocument(file)) { "Cannot open project.godot." }
        val exports = file.parent.findChild("export_presets.cfg")?.let(manager::getDocument)
        main to exports
    }

    override fun loadPreview(path: String, ready: (java.awt.image.BufferedImage?) -> Unit) {
        com.intellij.openapi.application.ReadAction.nonBlocking<java.awt.image.BufferedImage?> {
            GameArtwork.preview(file.parent, path)
        }.expireWith(owner).finishOnUiThread(com.intellij.openapi.application.ModalityState.any(), ready)
            .submit(com.intellij.util.concurrency.AppExecutorUtil.getAppExecutorService())
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
        for (resource in listOf(draft.mainScene, draft.icon, draft.splash, draft.exportIcon) +
            draft.exportDrafts.values.map { it.second }) {
            if (resource.startsWith("res://")) {
                require(file.parent.findFileByRelativePath(resource.removePrefix("res://")) != null) {
                    "Resource does not exist: $resource"
                }
            }
        }
        val files = listOfNotNull(file, file.parent.findChild("export_presets.cfg"))
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
                    main.setText(expected.projectText)
                    exports?.setText(expected.exportText)
                    throw error
                }
            },
        )
        return load().select(draft.preset)
    }
}
