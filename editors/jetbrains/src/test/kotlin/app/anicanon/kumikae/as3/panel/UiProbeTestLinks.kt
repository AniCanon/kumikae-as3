package app.anicanon.kumikae.as3.panel

import app.anicanon.kumikae.as3.testing.As3TestConsole
import com.intellij.execution.impl.ConsoleViewImpl
import com.intellij.execution.ui.ConsoleViewContentType
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import java.nio.file.Path

internal object UiProbeTestLinks {
    fun run(project: Project, properties: As3TestConsole, file: Path, line: Int) {
        lateinit var console: ConsoleViewImpl
        UiProbeCompletion.edt {
            console = properties.createConsole() as ConsoleViewImpl
            console.component
            console.print("\tat ProbeCase/run()[${file.fileName}:${line + 1}]\n", ConsoleViewContentType.ERROR_OUTPUT)
        }
        try {
            UiProbeCompletion.await("clickable AS3 failure frame") {
                console.getHyperlinks()?.hyperlinks?.isNotEmpty() ==
                    true
            }
            UiProbeCompletion.edt {
                checkNotNull(console.getHyperlinks()).hyperlinks.values.first().navigate(project)
                val editor = checkNotNull(FileEditorManager.getInstance(project).selectedTextEditor)
                check(FileDocumentManager.getInstance().getFile(editor.document)?.path == file.toString())
                check(editor.document.getLineNumber(editor.caretModel.offset) == line) {
                    "Stack hyperlink did not open the assertion line"
                }
                FileDocumentManager.getInstance().getFile(editor.document)?.let {
                    FileEditorManager.getInstance(project).closeFile(it)
                }
            }
        } finally {
            UiProbeCompletion.edt { Disposer.dispose(console) }
        }
    }
}
