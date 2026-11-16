package app.anicanon.kumikae.as3.settings

import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.IconLoader
import com.intellij.openapi.vfs.ReadonlyStatusHandler
import com.intellij.openapi.vfs.VirtualFile
import java.awt.image.BufferedImage

/** Uses IDE documents so Apply participates in undo and sees unsaved source edits. */
internal class IdeSettingsRepository(private val project: Project, private val file: VirtualFile) : SettingsRepository {
    private fun documents() = FileDocumentManager.getInstance().let { manager ->
        val main = requireNotNull(manager.getDocument(file)) { "Cannot open project.godot." }
        val exports = file.parent.findChild("export_presets.cfg")?.let(manager::getDocument)
        main to exports
    }

    override fun preview(path: String): BufferedImage? {
        if (!path.startsWith("res://")) return null
        val imageFile = file.parent.findFileByRelativePath(path.removePrefix("res://")) ?: return null
        if (imageFile.extension !in setOf("png", "svg", "jpg", "jpeg", "webp")) return null
        return runCatching {
            val icon = IconLoader.findIcon(imageFile.toNioPath().toUri().toURL()) ?: return null
            val image = IconLoader.toImage(icon) ?: return null
            val preview = BufferedImage(80, 80, BufferedImage.TYPE_INT_ARGB)
            val graphics = preview.createGraphics()
            try {
                val scale = 80.0 / maxOf(icon.iconWidth, icon.iconHeight).coerceAtLeast(1)
                val width = (icon.iconWidth * scale).toInt()
                val height = (icon.iconHeight * scale).toInt()
                graphics.drawImage(image, (80 - width) / 2, (80 - height) / 2, width, height, null)
            } finally {
                graphics.dispose()
            }
            preview
        }.getOrNull()
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
        for (resource in listOf(draft.mainScene, draft.icon, draft.exportIcon) +
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
