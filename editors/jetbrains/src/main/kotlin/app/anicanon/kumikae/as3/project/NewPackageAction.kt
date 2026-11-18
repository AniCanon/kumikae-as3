package app.anicanon.kumikae.as3.project

import com.intellij.ide.IdeView
import com.intellij.openapi.actionSystem.ActionUpdateThread
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.LangDataKeys
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.project.DumbAwareAction
import com.intellij.openapi.ui.InputValidator
import com.intellij.openapi.ui.Messages

class NewPackageAction : DumbAwareAction() {
    override fun getActionUpdateThread() = ActionUpdateThread.BGT
    override fun update(e: AnActionEvent) {
        e.presentation.isEnabledAndVisible = e.getData(LangDataKeys.IDE_VIEW)?.directories?.any { dir ->
            generateSequence(dir) { it.parentDirectory }.any { it.findFile("project.godot") != null }
        } == true
    }
    override fun actionPerformed(e: AnActionEvent) {
        val project = e.project ?: return
        val view: IdeView = e.getData(LangDataKeys.IDE_VIEW) ?: return
        val dir = view.orChooseDirectory ?: return
        val name = Messages.showInputDialog(
            project,
            "Package name",
            "New ActionScript Package",
            null,
            "",
            object : InputValidator {
                override fun checkInput(input: String) = input.split('.').all(As3Names::valid)
                override fun canClose(input: String) = checkInput(input)
            },
        ) ?: return
        WriteCommandAction.runWriteCommandAction(
            project,
            "Create ActionScript package",
            null,
            Runnable {
                val root = if (dir.findFile("project.godot") == null) {
                    dir
                } else {
                    dir.findSubdirectory("src") ?: dir.createSubdirectory("src")
                }
                val created = name.split('.').fold(root) { parent, part ->
                    parent.findSubdirectory(part) ?: parent.createSubdirectory(part)
                }
                view.selectElement(created)
            },
        )
    }
}
