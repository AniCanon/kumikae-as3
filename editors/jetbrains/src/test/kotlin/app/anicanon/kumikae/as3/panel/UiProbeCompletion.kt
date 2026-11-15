package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.language.As3LspSupportProvider
import com.intellij.codeInsight.CodeInsightSettings
import com.intellij.codeInsight.completion.CodeCompletionHandlerBase
import com.intellij.codeInsight.completion.CompletionType
import com.intellij.codeInsight.lookup.LookupManager
import com.intellij.ide.ApplicationActivationStateManager
import com.intellij.ide.DataManager
import com.intellij.ide.impl.OpenProjectTask
import com.intellij.ide.impl.ProjectUtil
import com.intellij.ide.impl.TrustedPathsSettings
import com.intellij.openapi.actionSystem.ActionManager
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.IdeActions
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.command.undo.UndoManager
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.DumbService
import com.intellij.openapi.project.ex.ProjectManagerEx
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.wm.WindowManager
import com.intellij.platform.lsp.api.LspClientManager
import com.intellij.psi.PsiDocumentManager
import java.awt.event.WindowEvent
import java.lang.management.ManagementFactory
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit

/** Native LSP lookup and acceptance in a disposable sandbox project. */
object UiProbeCompletion {
    fun run(destination: Path) {
        val heap = mutableListOf<String>()
        repeat(3) { cycle ->
            runProject(destination, cycle)
            System.gc()
            Thread.sleep(100)
            heap += "closed project $cycle heap=${ManagementFactory.getMemoryMXBean().heapMemoryUsage.used} bytes"
        }
        Files.write(destination.resolve("completion-heap.txt"), heap)
    }

    private fun runProject(destination: Path, cycle: Int) {
        val folder = Files.createTempDirectory("kas3-completion-probe")
        val source = "package { import godot.*; public class CompletionProbe extends Node2D {\n" +
            "public function run():void { transform.origin.normsuffix; } } }"
        val trust = TrustedPathsSettings.getInstance()
        val trustedPaths = trust.getTrustedPaths().toList()
        try {
            trust.addTrustedPath(folder.toString())
            checkProject(destination, cycle, folder, source)
        } finally {
            try {
                trust.setTrustedPaths(trustedPaths)
            } finally {
                check(folder.toFile().deleteRecursively()) { "Could not remove the probe project" }
            }
        }
    }

    private fun checkProject(destination: Path, cycle: Int, folder: Path, source: String) {
        val path = folder.resolve("CompletionProbe.as")
        Files.writeString(path, source)
        val project = checkNotNull(
            ProjectUtil.openOrImport(
                folder,
                OpenProjectTask {
                    forceOpenInNewFrame = true
                },
            ),
        )
        val observations = mutableListOf<String>()
        val settings = CodeInsightSettings.getInstance()
        val autoComplete = settings.AUTOCOMPLETE_ON_CODE_COMPLETION
        settings.AUTOCOMPLETE_ON_CODE_COMPLETION = false
        try {
            val file = checkNotNull(LocalFileSystem.getInstance().refreshAndFindFileByNioFile(path))
            lateinit var editor: Editor
            edt {
                FileEditorManager.getInstance(project).openFile(file, true)
                editor = checkNotNull(FileEditorManager.getInstance(project).selectedTextEditor)
                editor.caretModel.moveToOffset(source.indexOf("normsuffix") + 4)
            }
            await("LSP initialization") {
                LspClientManager.getInstance(project).getClients(As3LspSupportProvider::class.java)
                    .any { it.initializeResult != null }
            }
            await("indexing") { !DumbService.getInstance(project).isDumb }
            edt {
                editor = checkNotNull(FileEditorManager.getInstance(project).selectedTextEditor)
                editor.caretModel.moveToOffset(source.indexOf("normsuffix") + 4)
            }
            val times = mutableListOf<Double>()
            repeat(21) { sample ->
                val start = System.nanoTime()
                edt {
                    invokeCompletion(project, editor)
                }
                await("normalized lookup") {
                    LookupManager.getActiveLookup(editor)?.items?.any { it.lookupString.startsWith("normalized") } ==
                        true
                }
                times += (System.nanoTime() - start) / 1_000_000.0
                if (sample < 20) edt { LookupManager.getInstance(project).hideActiveLookup() }
            }
            val warm = times.drop(1).sorted()
            observations +=
                "lookup cold=${times.first()} ms median=${warm[10]} ms p95=${warm[18]} ms max=${warm.last()} ms"
            edt {
                val lookup = checkNotNull(LookupManager.getActiveLookup(editor))
                lookup.currentItem = lookup.items.single { it.lookupString.startsWith("normalized") }
                action(editor, IdeActions.ACTION_CHOOSE_LOOKUP_ITEM_REPLACE)
                check(!editor.document.text.contains("suffix")) {
                    "Tab left the identifier suffix: ${editor.document.text}"
                }
                check(editor.document.text.contains("transform.origin.normalized()")) { editor.document.text }
                observations += "Tab replaced suffix and inserted call"
                UndoManager.getInstance(project).undo(FileEditorManager.getInstance(project).selectedEditor)
                check(editor.document.text == source) {
                    "Undo did not restore completion source: ${editor.document.text}"
                }
                observations += "Undo restored the original identifier"
            }
            edt {
                WriteCommandAction.runWriteCommandAction(project) { editor.document.setText(source) }
                editor.caretModel.moveToOffset(source.indexOf("normsuffix") + 4)
                invokeCompletion(project, editor)
            }
            await("Enter lookup") {
                LookupManager.getActiveLookup(editor)?.items?.any { it.lookupString.startsWith("normalized") } == true
            }
            edt {
                val lookup = checkNotNull(LookupManager.getActiveLookup(editor))
                lookup.currentItem = lookup.items.single { it.lookupString.startsWith("normalized") }
                action(editor, IdeActions.ACTION_CHOOSE_LOOKUP_ITEM)
                check(editor.document.text.contains("transform.origin.normalized()")) { editor.document.text }
                check(editor.document.text.contains("normalized()suffix")) { "Enter lost the suffix" }
                observations += "Enter preserves the suffix"
            }
            edt {
                WriteCommandAction.runWriteCommandAction(project) { editor.document.setText(source) }
                editor.caretModel.moveToOffset(source.indexOf("normsuffix") + 4)
                invokeCompletion(project, editor)
            }
            val latest = source.replace("normsuffix", "xsuffix")
            edt {
                WriteCommandAction.runWriteCommandAction(project) { editor.document.setText(latest) }
                editor.caretModel.moveToOffset(latest.indexOf("xsuffix") + 1)
                invokeCompletion(project, editor)
            }
            await("latest document lookup") {
                LookupManager.getActiveLookup(editor)?.items?.any { it.lookupString == "x" } == true
            }
            edt {
                check(
                    LookupManager.getActiveLookup(editor)?.items?.none { it.lookupString.startsWith("normalized") } ==
                        true,
                )
                LookupManager.getInstance(project).hideActiveLookup()
                observations += "Changed document cancels the old lookup and offers x"
            }
            checkSnippet(project, editor, source)
            observations += "Required argument is selected; Tab exits the snippet after the call"
        } finally {
            settings.AUTOCOMPLETE_ON_CODE_COMPLETION = autoComplete
            try {
                edt { observations += "final text: ${editorText(project)}" }
            } finally {
                try {
                    edt { ProjectManagerEx.getInstanceEx().forceCloseProject(project, false) }
                } finally {
                    Files.write(destination.resolve("completion-$cycle.txt"), observations)
                }
            }
        }
    }

    private fun checkSnippet(project: com.intellij.openapi.project.Project, editor: Editor, source: String) {
        val call = source.replace("public function run", "public function move(amount:int):void {} public function run")
            .replace("transform.origin.normsuffix", "movsuffix")
        edt {
            WriteCommandAction.runWriteCommandAction(project) { editor.document.setText(call) }
            editor.caretModel.moveToOffset(call.indexOf("movsuffix") + 3)
            invokeCompletion(project, editor)
        }
        await("call snippet") {
            LookupManager.getActiveLookup(editor)?.items?.any { it.lookupString.startsWith("move(") } == true
        }
        edt {
            val lookup = checkNotNull(LookupManager.getActiveLookup(editor))
            lookup.currentItem = lookup.items.single { it.lookupString.startsWith("move(") }
            action(editor, IdeActions.ACTION_CHOOSE_LOOKUP_ITEM_REPLACE)
            check(editor.selectionModel.selectedText == "amount") { "Required argument is not selected" }
            action(editor, IdeActions.ACTION_EDITOR_NEXT_TEMPLATE_VARIABLE)
        }
        await("snippet end") {
            editor.caretModel.offset == editor.document.text.indexOf("move(amount)") + "move(amount)".length
        }
    }

    private fun action(editor: Editor, id: String) {
        val context = DataManager.getInstance().getDataContext(editor.contentComponent)
        ActionManager.getInstance().getAction(id).actionPerformed(
            AnActionEvent.createFromDataContext("UI probe", null, context),
        )
    }

    private fun invokeCompletion(project: com.intellij.openapi.project.Project, editor: Editor) {
        // A Gradle sandbox can start in the background. Deliver activation to its own frame so the
        // platform's hint manager permits showing the real lookup without stealing desktop focus.
        val frame = checkNotNull(WindowManager.getInstance().getFrame(project))
        ApplicationActivationStateManager.updateState(WindowEvent(frame, WindowEvent.WINDOW_ACTIVATED))
        PsiDocumentManager.getInstance(project).commitAllDocuments()
        CodeCompletionHandlerBase(CompletionType.BASIC).invokeCompletion(project, editor)
    }

    private fun editorText(project: com.intellij.openapi.project.Project): String =
        FileEditorManager.getInstance(project).selectedTextEditor?.let {
            "${it.document.text}; caret=${it.caretModel.offset}; selected=${it.selectionModel.selectedText}; " +
                "lookup=${LookupManager.getActiveLookup(it)?.items?.map { item -> item.lookupString }}"
        }.orEmpty()

    private fun edt(action: () -> Unit) = ApplicationManager.getApplication().invokeAndWait(action)

    private fun await(name: String, condition: () -> Boolean) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30)
        while (System.nanoTime() < deadline) {
            var ready = false
            edt { ready = condition() }
            if (ready) return
            Thread.sleep(10)
        }
        error("Timed out waiting for $name")
    }
}
