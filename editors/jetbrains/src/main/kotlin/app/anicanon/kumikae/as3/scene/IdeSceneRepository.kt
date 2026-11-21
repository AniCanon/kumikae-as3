package app.anicanon.kumikae.as3.scene

import app.anicanon.kumikae.as3.language.As3Lexer
import app.anicanon.kumikae.as3.language.As3Tokens
import app.anicanon.kumikae.as3.run.IdeGodot
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.Messages
import com.intellij.openapi.vfs.VfsUtilCore
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.util.concurrency.AppExecutorUtil

internal fun resolveProjectResource(root: VirtualFile, path: String): VirtualFile? {
    if (!path.startsWith("res://")) return null
    val relative = path.removePrefix("res://")
    if (relative.isEmpty() || '\\' in relative || relative.split('/').any { it.isEmpty() || it == "." || it == ".." }) {
        return null
    }
    val found = root.findFileByRelativePath(relative) ?: return null
    return found.takeIf { VfsUtilCore.isAncestor(root, it, true) }
}

internal class IdeSceneRepository(private val project: Project, private val file: VirtualFile) : SceneRepository {
    private fun document() = requireNotNull(FileDocumentManager.getInstance().getDocument(file)) {
        "Cannot open scene source."
    }
    private fun root() =
        generateSequence(file.parent) { it.parent }.firstOrNull { it.findChild("project.godot") != null }
            ?: error("Scene must belong to a Godot project.")
    private fun resource(path: String) = resolveProjectResource(root(), path)
        ?: error("Resource not found: $path")

    override fun openInGodot() {
        FileDocumentManager.getInstance().saveDocument(document())
        val folder = root()
        val relative = requireNotNull(VfsUtilCore.getRelativePath(file, folder))
        val folderPath = folder.path
        AppExecutorUtil.getAppExecutorService().execute {
            val error = try {
                val godot = IdeGodot.find() ?: error("Install Godot to open its scene editor.")
                ProcessBuilder(godot.toString(), "--editor", "--path", folderPath, "res://$relative")
                    .redirectOutput(ProcessBuilder.Redirect.DISCARD)
                    .redirectError(ProcessBuilder.Redirect.DISCARD)
                    .start()
                null
            } catch (failure: Exception) {
                failure.message ?: "Could not open Godot."
            }
            if (error != null) {
                ApplicationManager.getApplication().invokeLater {
                    if (!project.isDisposed) Messages.showErrorDialog(project, error, "Open in Godot")
                }
            }
        }
    }
    override fun read() = document().text
    override fun open(path: String) {
        FileEditorManager.getInstance(project).openFile(resource(path), true)
    }

    override fun attach(expected: String, node: SceneNode, path: String?): String {
        val document = document()
        check(document.text == expected) { "Scene changed. Reload before editing attachments." }
        check(file.isWritable) { "Scene is read-only." }
        if (path != null) {
            val script =
                requireNotNull(FileDocumentManager.getInstance().getDocument(resource(path))) {
                    "Cannot read AS3 script."
                }
            val lexer = As3Lexer()
            lexer.start(script.text)
            val code = StringBuilder()
            while (lexer.tokenType != null) {
                if (lexer.tokenType !in IGNORED_TOKENS) code.append(lexer.tokenText) else code.append(' ')
                lexer.advance()
            }
            val declaration = CLASS_EXTENDS.find(code)
            val base = declaration?.groupValues?.get(1)
            val imports = IMPORT_STMT.findAll(code).map { it.groupValues[1] }.distinct().toList()
            val explicitNative =
                imports.filter { it.substringAfterLast('.') == node.type } == listOf("godot.${node.type}")
            require(base == "godot.${node.type}" || (base == node.type && explicitNative)) {
                "Choose a script extending godot.${node.type} directly, with an explicit native import. " +
                    "Other inheritance needs verification in Godot."
            }
        }
        val updated = SceneDocument(expected).attach(node.path, path)
        WriteCommandAction.runWriteCommandAction(
            project,
            "Change AS3 scene attachment",
            null,
            Runnable {
                check(document.text == expected) { "Scene changed. Reload before editing attachments." }
                document.setText(updated)
            },
        )
        return updated
    }

    private companion object {
        val IGNORED_TOKENS = setOf(
            As3Tokens.LINE_COMMENT,
            As3Tokens.BLOCK_COMMENT,
            As3Tokens.STRING,
            As3Tokens.REGEX,
        )
        val CLASS_EXTENDS = Regex("\\bclass\\s+\\w+\\s+extends\\s+([\\w.]+)")
        val IMPORT_STMT = Regex("\\bimport\\s+([\\w.]+)\\s*;?")
    }
}
