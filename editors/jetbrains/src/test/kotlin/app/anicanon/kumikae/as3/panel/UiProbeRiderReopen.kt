package app.anicanon.kumikae.as3.panel

import com.intellij.ide.impl.OpenProjectTask
import com.intellij.ide.impl.ProjectUtil
import com.intellij.ide.impl.TrustedPathsSettings
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.wm.ToolWindowManager
import com.intellij.openapi.wm.impl.DesktopLayout
import com.intellij.openapi.wm.impl.ToolWindowManagerImpl
import java.lang.ref.WeakReference
import java.nio.file.Files
import java.nio.file.Path

internal object UiProbeRiderReopen {
    fun run(destination: Path, observeNativePanels: Boolean = false) {
        val report = Files.createDirectories(destination.resolve("reopened-project"))
        var survivingPanels = 0
        val projects = exercise(report, plainText = observeNativePanels) { project, observations ->
            if (observeNativePanels) {
                survivingPanels += UiProbeRiderPanels.observeClose(project, observations)
            } else {
                UiProbeRetention.closeProject(project, observations)
            }
        }
        UiProbeRetention.checkReleased(projects, report)
        check(survivingPanels == 0) { "$survivingPanels Current File backend panels survived close" }
    }

    private fun exercise(
        report: Path,
        plainText: Boolean,
        close: (Project, MutableList<String>) -> Unit,
    ): List<WeakReference<Project>> {
        val observations = mutableListOf<String>()
        val folder = Files.createTempDirectory("kas3-reopen-project")
        val trust = TrustedPathsSettings.getInstance()
        val previousTrust = trust.getTrustedPaths().toList()
        val references = mutableListOf<WeakReference<Project>>()
        var tabs: List<String>? = null
        var layout: DesktopLayout? = null
        val layoutType = Class.forName("com.jetbrains.rider.settings.perApp.wm.RiderPerAppToolWindowLayoutManager")
        val layoutOwner = ApplicationManager.getApplication().getService(layoutType)
        val setLayout = layoutType.getMethod("setLayout", DesktopLayout::class.java)
        val originalLayout = layoutType.getMethod("getLayoutCopy").invoke(layoutOwner) as DesktopLayout
        try {
            Files.createDirectories(folder.resolve(".idea"))
            Files.writeString(folder.resolve(".idea/.gitignore"), "*\n")
            val fixture = if (plainText) folder.resolve("Reopen.txt") else folder.resolve("Reopen.as")
            Files.writeString(
                fixture,
                if (plainText) "Rider project reopen fixture" else "package { public class Reopen {} }",
            )
            trust.addTrustedPath(folder.toString())
            repeat(2) { cycle ->
                if (cycle == 1) UiProbeDispatch.edt { setLayout.invoke(layoutOwner, checkNotNull(layout)) }
                val project =
                    checkNotNull(ProjectUtil.openOrImport(folder, OpenProjectTask { forceOpenInNewFrame = true }))
                references += WeakReference(project)
                try {
                    val file =
                        checkNotNull(
                            LocalFileSystem.getInstance().refreshAndFindFileByNioFile(fixture),
                        )
                    UiProbeDispatch.edt {
                        FileEditorManager.getInstance(project).openFile(file, true)
                        if (cycle == 0) {
                            checkNotNull(
                                ToolWindowManager.getInstance(project).getToolWindow("Problems View"),
                            ).show()
                        }
                    }
                    UiProbeDispatch.await("restored visible Problems window") {
                        ToolWindowManager.getInstance(project).getToolWindow("Problems View")?.isVisible == true
                    }
                    // The layout records a shown window after the window reports itself visible.
                    UiProbeDispatch.await("visible Problems window in the layout") {
                        (ToolWindowManager.getInstance(project) as ToolWindowManagerImpl).getLayout()
                            .getInfo("Problems View")?.isVisible == true
                    }
                    UiProbeDispatch.edt {
                        UiProbeRetention.checkSplitView(project, observations)
                        val window = checkNotNull(ToolWindowManager.getInstance(project).getToolWindow("Problems View"))
                        val contents = window.contentManager.contents
                        check(
                            contents.first().component.javaClass.name ==
                                "com.intellij.platform.problemsView.frontend.FrontendHighlightingPanel",
                        ) {
                            "Current File tab moved: ${contents.map { it.displayName }}"
                        }
                        if (cycle == 0) {
                            layout =
                                (ToolWindowManager.getInstance(project) as ToolWindowManagerImpl).getLayout().copy()
                            check(layout.getInfo("Problems View")?.isVisible == true) {
                                "Layout lost the Problems window"
                            }
                        }
                        val current = contents.map { it.component.javaClass.name }
                        if (tabs == null) {
                            tabs = current
                        } else {
                            check(tabs == current) { "Reopen changed Problems tab order" }
                        }
                    }
                } finally {
                    UiProbeDispatch.edt { close(project, observations) }
                }
            }
            observations +=
                "Captured visible split Problems layout restored before project opening with the same tab order"
            return references
        } finally {
            UiProbeDispatch.edt { setLayout.invoke(layoutOwner, originalLayout) }
            trust.setTrustedPaths(previousTrust)
            Files.write(report.resolve("result.txt"), observations)
            check(folder.toFile().deleteRecursively()) { "Cannot remove reopened project fixture" }
        }
    }
}
