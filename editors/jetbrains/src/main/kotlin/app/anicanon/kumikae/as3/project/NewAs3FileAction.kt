package app.anicanon.kumikae.as3.project

import app.anicanon.kumikae.as3.PluginFiles
import com.intellij.ide.actions.CreateFileFromTemplateAction
import com.intellij.ide.actions.CreateFileFromTemplateDialog
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.project.Project
import com.intellij.psi.PsiDirectory

/** Add > AS3 Class: a new script named after its class, as the language requires. */
class NewAs3FileAction :
    CreateFileFromTemplateAction("AS3 Class", "A new ActionScript 3 class", PluginFiles.script),
    DumbAware {
    override fun buildDialog(project: Project, directory: PsiDirectory, builder: CreateFileFromTemplateDialog.Builder) {
        builder.setTitle("New AS3 Class")
            .addKind("Script for a node", PluginFiles.script, "AS3 Node Script")
            .addKind("Class", PluginFiles.script, "AS3 Class")
    }

    override fun getActionName(directory: PsiDirectory?, newName: String, templateName: String?) =
        "Create AS3 Class $newName"
}
