package app.anicanon.kumikae.as3.scene

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.TitleButton
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.ide.structureView.StructureViewTreeElement
import com.intellij.ide.structureView.TreeBasedStructureViewBuilder
import com.intellij.lang.LanguageStructureViewBuilder
import com.intellij.openapi.application.ModalityState
import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileEditor.FileEditor
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.fileEditor.FileEditorPolicy
import com.intellij.openapi.fileEditor.FileEditorProvider
import com.intellij.openapi.fileEditor.FileEditorState
import com.intellij.openapi.fileEditor.OpenFileDescriptor
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.util.UserDataHolderBase
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiManager
import com.intellij.util.concurrency.AppExecutorUtil
import com.intellij.util.ui.JBFont
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.components.layout.ScrollPane
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.modifier.appearance.emptyBorder
import org.jetbrains.compose.swing.modifier.appearance.font
import java.beans.PropertyChangeListener

class GdScriptEditorProvider :
    FileEditorProvider,
    DumbAware {
    override fun accept(project: Project, file: VirtualFile) = file.extension == "gd" &&
        generateSequence(file.parent) { it.parent }.any { it.findChild("project.godot") != null }
    override fun createEditor(project: Project, file: VirtualFile): FileEditor = GdScriptOverview(project, file)
    override fun getEditorTypeId() = "kumikae.gdscript"
    override fun getPolicy() = FileEditorPolicy.PLACE_BEFORE_DEFAULT_EDITOR
}

private data class ScriptMember(val name: String, val offset: Int)
private data class ScriptOutline(val members: List<ScriptMember>, val scenes: List<SceneUsage>)

private class GdScriptOverview(private val project: Project, private val script: VirtualFile) :
    UserDataHolderBase(),
    FileEditor {
    private val files get() = FileEditorManager.getInstance(project)
    private var outline by mutableStateOf<ScriptOutline?>(null)
    private val panel = composePanel(this) {
        ScrollPane {
            Panel(PanelLayout.Border(), modifier = SwingModifier.viewport()) {
                Column(SwingModifier.north().emptyBorder(JBUI.insets(20))) {
                    Label(script.nameWithoutExtension, SwingModifier.font(JBFont.h2()))
                    Button("Edit source", { source(0) })
                    val data = outline
                    if (data == null) {
                        StatusLine("Reading script…")
                    } else {
                        Label("Declarations", SwingModifier.font(JBFont.h3()).emptyBorder(JBUI.insetsTop(18)))
                        if (data.members.isEmpty()) StatusLine("No declarations")
                        data.members.forEach { member ->
                            TitleButton(member.name, "", { source(member.offset) }, chevron = false)
                        }
                        Label("Attached scenes", SwingModifier.font(JBFont.h3()).emptyBorder(JBUI.insetsTop(18)))
                        if (data.scenes.isEmpty()) StatusLine("No scene attachments")
                        data.scenes.forEach { usage ->
                            TitleButton(
                                usage.file.name,
                                usage.node.path,
                                { files.openFile(usage.file, true) },
                                chevron = false,
                            )
                        }
                    }
                }
            }
        }
    }
    private fun source(offset: Int) {
        files.openTextEditor(OpenFileDescriptor(project, script, offset), true)
    }
    override fun selectNotify() {
        ReadAction.nonBlocking<ScriptOutline> {
            ScriptOutline(members(), sceneUsages(project, script))
        }.inSmartMode(project).expireWith(this).coalesceBy(this, "script-outline")
            .finishOnUiThread(ModalityState.defaultModalityState()) {
                outline = it
            }.submit(AppExecutorUtil.getAppExecutorService())
    }
    private fun members(): List<ScriptMember> {
        val psi = PsiManager.getInstance(project).findFile(script)
        val builder = psi?.let {
            LanguageStructureViewBuilder.getInstance().getStructureViewBuilder(it)
        } as? TreeBasedStructureViewBuilder
        val model = builder?.createStructureViewModel(null)
        if (model != null) {
            try {
                val members = model.root.children.mapNotNull { child ->
                    val element = (child as? StructureViewTreeElement)?.value as? PsiElement
                    element?.let {
                        ScriptMember(child.presentation.presentableText ?: return@mapNotNull null, it.textOffset)
                    }
                }
                if (members.isNotEmpty()) return members
            } finally {
                Disposer.dispose(model)
            }
        }
        val document = FileDocumentManager.getInstance().getDocument(script) ?: return emptyList()
        return document.immutableCharSequence.lineSequence().mapIndexedNotNull { line, text ->
            DECLARATION_PATTERN.find(text)?.let { match ->
                ScriptMember(match.value.trim(), document.getLineStartOffset(line))
            }
        }.toList()
    }
    override fun getFile() = script
    override fun getComponent() = panel
    override fun getPreferredFocusedComponent() = panel
    override fun getName() = "Overview"
    override fun setState(state: FileEditorState) = Unit
    override fun isModified() = false
    override fun isValid() = script.isValid
    override fun addPropertyChangeListener(listener: PropertyChangeListener) = Unit
    override fun removePropertyChangeListener(listener: PropertyChangeListener) = Unit
    override fun dispose() = Unit

    private companion object {
        val DECLARATION_PATTERN = Regex(
            "^\\s*(?:@\\w+(?:\\([^)]*\\))?\\s+)*(?:(?:static|abstract)\\s+)?(func|var|const|signal|enum|class|class_name)\\s+([A-Za-z_]\\w*)",
        )
    }
}
