package app.anicanon.kumikae.as3.project

import app.anicanon.kumikae.as3.PluginFiles
import app.anicanon.kumikae.as3.settings.GodotConfig
import com.intellij.icons.AllIcons
import com.intellij.ide.actions.CreateFileFromTemplateAction
import com.intellij.ide.actions.CreateFileFromTemplateDialog
import com.intellij.ide.fileTemplates.FileTemplate
import com.intellij.ide.fileTemplates.FileTemplateManager
import com.intellij.ide.fileTemplates.FileTemplateUtil
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.InputValidator
import com.intellij.psi.PsiDirectory
import com.intellij.psi.PsiFile

class NewAs3FileAction :
    CreateFileFromTemplateAction("ActionScript", "Create an ActionScript file", PluginFiles.script),
    DumbAware {
    override fun buildDialog(project: Project, directory: PsiDirectory, builder: CreateFileFromTemplateDialog.Builder) {
        builder.setTitle("New ActionScript")
            .addKind("ActionScript File", PluginFiles.script, "AS3 File")
            .addKind("ActionScript Class", AllIcons.Nodes.Class, "AS3 Class")
            .addKind("ActionScript Interface", AllIcons.Nodes.Interface, "AS3 Interface")
            .addKind("Godot Node Script", PluginFiles.mark, "AS3 Node Script")
            .addKind("Test Case Class", AllIcons.RunConfigurations.TestState.Run, "AS3 Test Case")
            .addKind("Test Suite Class", AllIcons.Modules.TestRoot, "AS3 Test Suite")
            .setValidator(object : InputValidator {
                override fun checkInput(input: String) = As3Names.valid(input.removeSuffix(".as"))
                override fun canClose(input: String) = checkInput(input)
            })
    }

    public override fun createFileFromTemplate(name: String, template: FileTemplate, dir: PsiDirectory): PsiFile {
        val typeName = name.removeSuffix(".as")
        val ancestors = generateSequence(dir) { it.parentDirectory }.toList()
        val game = ancestors.firstOrNull { it.findFile("project.godot") != null }
        val sourceRoot = ancestors.firstOrNull { it.parentDirectory == game && it.name in setOf("src", "tests") }
        val isTest = template.name.startsWith("AS3 Test")
        val defaultPackage = game?.findFile("project.godot")?.virtualFile?.let {
            FileDocumentManager.getInstance().getDocument(it)?.text
        }?.let { GodotConfig(it).string("application", "config/kumikae_package") }?.takeIf { it.isNotBlank() }
            ?: game?.findSubdirectory("src")?.let { src ->
                generateSequence(src) { folder ->
                    folder.subdirectories.singleOrNull()?.takeIf {
                        folder.files.none { file -> file.name.endsWith(".as") } && As3Names.valid(it.name)
                    }
                }.drop(1).joinToString(".") { it.name }
            }.orEmpty()
        val namespace = defaultPackage.replace('-', '_')
        val relative = ancestors.takeWhile { it != sourceRoot }.asReversed().map { it.name }
        val selected = if (sourceRoot != null &&
            relative.isNotEmpty()
        ) {
            relative
        } else {
            namespace.split('.').filter(String::isNotEmpty)
        }
        val node = template.name == "AS3 Node Script"
        val parts = if (node && "view" !in selected) selected + "view" else selected
        require(parts.all(As3Names::valid)) { "Choose a directory with a valid ActionScript package name." }
        val destination = if (game != null) {
            val folder = if (isTest || (!node && sourceRoot?.name == "tests")) "tests" else "src"
            (listOf(folder) + parts).fold(game) { parent, child ->
                parent.findSubdirectory(child) ?: parent.createSubdirectory(child)
            }
        } else {
            dir
        }
        val properties = FileTemplateManager.getInstance(dir.project).defaultProperties
        properties.setProperty("AS3_PACKAGE", parts.joinToString(".").let { if (it.isEmpty()) "" else " $it" })
        return FileTemplateUtil.createFromTemplate(template, typeName, properties, destination) as PsiFile
    }

    override fun getActionName(directory: PsiDirectory?, newName: String, templateName: String?) = "Create $newName"
}
