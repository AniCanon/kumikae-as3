package app.anicanon.kumikae.as3.panel

import com.intellij.codeInsight.CodeInsightSettings
import com.intellij.codeInsight.lookup.LookupManager
import com.intellij.ide.GeneralSettings
import com.intellij.ide.impl.OpenProjectTask
import com.intellij.ide.impl.ProjectUtil
import com.intellij.ide.impl.TrustedPathsSettings
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.project.ex.ProjectManagerEx
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.wm.WindowManager
import com.intellij.openapi.wm.impl.IdeFrameImpl
import java.lang.ref.WeakReference
import java.nio.file.Files
import java.nio.file.Path

internal object UiProbeConcurrentProjects {
    fun run(destination: Path) {
        val report = Files.createDirectories(destination.resolve("concurrent-projects"))
        val references = exercise(report)
        UiProbeRetention.checkReleased(references, report)
    }

    private fun exercise(report: Path): List<WeakReference<Project>> {
        val folder = Files.createTempDirectory("kas3-concurrent-projects")
        val trust = TrustedPathsSettings.getInstance()
        val originalTrust = trust.getTrustedPaths().toList()
        val projects = mutableListOf<Project>()
        val settings = CodeInsightSettings.getInstance()
        val previousAutoComplete = settings.AUTOCOMPLETE_ON_CODE_COMPLETION
        val opening = GeneralSettings.getInstance()
        val previousOpening = opening.confirmOpenNewProject
        val observations = mutableListOf<String>()
        settings.AUTOCOMPLETE_ON_CODE_COMPLETION = false
        try {
            repeat(2) { index ->
                val path = Files.createDirectories(folder.resolve("project-$index"))
                Files.createDirectories(path.resolve(".idea"))
                Files.writeString(path.resolve(".idea/.gitignore"), "*\n")
                Files.writeString(path.resolve("Concurrent.as"), SOURCE)
                trust.addTrustedPath(path.toString())
                projects += checkNotNull(ProjectUtil.openOrImport(path, OpenProjectTask { forceOpenInNewFrame = true }))
            }
            val editors = projects.mapIndexed { index, project ->
                val file = checkNotNull(
                    LocalFileSystem.getInstance().refreshAndFindFileByNioFile(
                        folder.resolve("project-$index/Concurrent.as"),
                    ),
                )
                lateinit var editor: Editor
                UiProbeCompletion.edt {
                    FileEditorManager.getInstance(project).openFile(file, true)
                    editor = checkNotNull(FileEditorManager.getInstance(project).selectedTextEditor)
                    editor.caretModel.moveToOffset(SOURCE.indexOf("normsuffix") + 4)
                }
                UiProbeCompletion.await("concurrent project client") { UiProbeLsp.ready(project) }
                editor
            }
            val active = projects[1]
            val editor = editors[1]
            val frame = WindowManager.getInstance().getFrame(active) as IdeFrameImpl
            val helper = frame.frameHelper
            val root = frame.rootPane
            UiProbeCompletion.edt {
                UiProbeRetention.checkSplitView(projects[0], observations)
                check(ProjectManagerEx.getInstanceEx().forceCloseProject(projects[0], false))
            }
            // Drain deferred cleanup before checking the surviving project's frame and native lookup.
            UiProbeCompletion.edt {
                check(frame.frameHelper === helper && frame.rootPane === root && !active.isDisposed)
                check(editor.document.text == SOURCE)
                UiProbeRetention.checkSplitView(active, observations)
            }
            checkCompletion(active, editor, observations)
            observations += "Closing the other project preserves this frame, source, split view and native completion"
            val replacementPath = Files.createDirectories(folder.resolve("replacement"))
            Files.createDirectories(replacementPath.resolve(".idea"))
            Files.writeString(replacementPath.resolve(".idea/.gitignore"), "*\n")
            Files.writeString(replacementPath.resolve("Concurrent.as"), SOURCE)
            trust.addTrustedPath(replacementPath.toString())
            // Rider's folder importer reads the normal setting rather than forceReuseFrame.
            opening.confirmOpenNewProject = GeneralSettings.OPEN_PROJECT_SAME_WINDOW
            val replacement = checkNotNull(
                ProjectUtil.openOrImport(
                    replacementPath,
                    OpenProjectTask {
                        projectToClose = active
                        forceReuseFrame = true
                    },
                ),
            )
            projects += replacement
            lateinit var replacementEditor: Editor
            UiProbeCompletion.edt {
                check(active.isDisposed) { "Frame reuse did not close the previous project" }
                check(WindowManager.getInstance().getFrame(replacement) === frame) { "Frame was not reused" }
                check(frame.frameHelper?.project === replacement && frame.rootPane !== root)
                val file =
                    checkNotNull(
                        LocalFileSystem.getInstance().refreshAndFindFileByNioFile(
                            replacementPath.resolve("Concurrent.as"),
                        ),
                    )
                FileEditorManager.getInstance(replacement).openFile(file, true)
                replacementEditor = checkNotNull(FileEditorManager.getInstance(replacement).selectedTextEditor)
                replacementEditor.caretModel.moveToOffset(SOURCE.indexOf("normsuffix") + 4)
                UiProbeRetention.checkSplitView(replacement, observations)
            }
            UiProbeCompletion.await("reused frame client") { UiProbeLsp.ready(replacement) }
            checkCompletion(replacement, replacementEditor, observations)
            observations += "Opening in the same frame preserves the new project root, split view and completion"
            return projects.map { WeakReference(it) }
        } finally {
            try {
                UiProbeCompletion.edt {
                    projects.filterNot { it.isDisposed }.forEach { project ->
                        check(ProjectManagerEx.getInstanceEx().forceCloseProject(project, false))
                    }
                }
            } finally {
                settings.AUTOCOMPLETE_ON_CODE_COMPLETION = previousAutoComplete
                opening.confirmOpenNewProject = previousOpening
                trust.setTrustedPaths(originalTrust)
                Files.write(report.resolve("result.txt"), observations)
                check(folder.toFile().deleteRecursively()) { "Cannot remove concurrent project fixtures" }
            }
        }
    }

    private fun checkCompletion(project: Project, editor: Editor, observations: MutableList<String>) {
        UiProbeLookup.complete(project, editor, "surviving project native lookup", observations) {
            it.lookupString.startsWith("normalized")
        }
        UiProbeCompletion.edt { LookupManager.getInstance(project).hideActiveLookup() }
    }

    private const val SOURCE = "package { import godot.*; public class Concurrent extends Node2D { " +
        "public function run():void { transform.origin.normsuffix; } } }"
}
