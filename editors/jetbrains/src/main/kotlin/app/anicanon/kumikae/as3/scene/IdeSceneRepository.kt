package app.anicanon.kumikae.as3.scene

import app.anicanon.kumikae.as3.language.As3Lexer
import app.anicanon.kumikae.as3.language.As3Tokens
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile

internal class IdeSceneRepository(private val project: Project, private val file: VirtualFile) : SceneRepository {
    private fun document() = requireNotNull(FileDocumentManager.getInstance().getDocument(file)) {
        "Cannot open scene source."
    }
    private fun root() =
        generateSequence(file.parent) { it.parent }.firstOrNull { it.findChild("project.godot") != null }
            ?: error("Scene must belong to a Godot project.")
    private fun resource(path: String) = root().findFileByRelativePath(path.removePrefix("res://"))
        ?: error("Resource not found: $path")

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
                if (lexer.tokenType !in
                    setOf(As3Tokens.LINE_COMMENT, As3Tokens.BLOCK_COMMENT, As3Tokens.STRING, As3Tokens.REGEX)
                ) {
                    code.append(lexer.tokenText)
                } else {
                    code.append(' ')
                }
                lexer.advance()
            }
            val declaration = Regex("\\bclass\\s+\\w+\\s+extends\\s+([\\w.]+)").find(code)
            val base = declaration?.groupValues?.get(1)
            val imports = Regex("\\bimport\\s+([\\w.]+)\\s*;").findAll(code).map { it.groupValues[1] }.toList()
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
}
