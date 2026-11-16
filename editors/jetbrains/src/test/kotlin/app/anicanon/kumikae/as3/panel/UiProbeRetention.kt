package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.Disposable
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.project.ex.ProjectManagerEx
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.wm.ToolWindowManager
import com.intellij.util.PlatformUtils
import java.awt.Container
import java.awt.KeyboardFocusManager
import java.lang.ref.WeakReference
import java.nio.file.Files
import java.nio.file.Path
import javax.swing.JWindow

/** Only weak references survive project closure; live histograms force collection in this test JVM. */
internal object UiProbeRetention {
    fun enabled(): Boolean = System.getenv("KUMIKAE_COMPLETION_RETENTION_CHECK") == "1" ||
        System.getenv("KUMIKAE_COMPLETION_RETENTION_PROBE") == "1"

    fun closeProject(project: Project, observations: MutableList<String>) {
        var focusRoot: Container? = null
        var panels: List<Disposable> = emptyList()
        val focusManager = KeyboardFocusManager.getCurrentKeyboardFocusManager()
        try {
            checkSplitView(project, observations)
            if (enabled() && PlatformUtils.isRider()) {
                panels = UiProbeRiderPanels.capture(project)
                val editor = checkNotNull(FileEditorManager.getInstance(project).selectedTextEditor)
                focusRoot = checkNotNull(editor.contentComponent.focusCycleRootAncestor)
                check(focusRoot.javaClass.name == "com.intellij.openapi.fileEditor.impl.EditorCompositePanel")
                // Make the intermittent native focus root retention reproducible on every close.
                focusManager.setGlobalCurrentFocusCycleRoot(focusRoot)
            }
        } finally {
            check(ProjectManagerEx.getInstanceEx().forceCloseProject(project, false))
            check(project.isDisposed) { "Closed probe project was not disposed" }
            observations += "Project closed and disposed"
        }
        check(panels.all(::disposed)) { "Native Current File panel survived project close" }
        if (panels.isNotEmpty()) observations += "Native Current File panel owned by its tool window and disposed"
        if (focusRoot != null) {
            check(focusManager.currentFocusCycleRoot !== focusRoot) {
                "Closed editor remains the global focus cycle root"
            }
            observations += "Closed editor released from global focus cycle root"
        }
    }

    fun checkSplitView(project: Project, observations: MutableList<String>) {
        if (!enabled() || !PlatformUtils.isRider()) return
        val window = checkNotNull(ToolWindowManager.getInstance(project).getToolWindow("Problems View"))
        check(
            window.contentManager.contents.any {
                it.component.javaClass.name == "com.intellij.platform.problemsView.frontend.FrontendHighlightingPanel"
            },
        ) { "The retention check requires Rider's split Problems view" }
        observations += "Split Problems view remains enabled"
    }

    fun checkReleased(projects: List<WeakReference<Project>>, destination: Path) {
        var histogram = ""
        // Rider's error-icon queue has a native five-second timeout after its component disappears.
        val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(10)
        do {
            UiProbeCompletion.edt { paintShowingWindows() }
            histogram = UiProbeHeap.collect()
            if (recordRetained(projects, destination) == 0) return
            Thread.sleep(200)
        } while (System.nanoTime() < deadline)
        Files.writeString(destination.resolve("retained-projects.histo"), histogram)
        if (System.getenv("KUMIKAE_COMPLETION_HEAP_DUMP") == "1") {
            java.lang.management.ManagementFactory.getPlatformMXBean(
                com.sun.management.HotSpotDiagnosticMXBean::class.java,
            )
                .dumpHeap(destination.resolve("retained-${System.nanoTime()}.hprof").toString(), true)
        }
        // A live heap dump can itself collect the final reference; report the latest observation.
        val retained = recordRetained(projects, destination)
        check(retained == 0) { "$retained of ${projects.size} closed projects remain reachable after full GC" }
    }

    /**
     * Swing's buffer-strategy paint manager keeps the root pane it painted last until it paints another.
     * When that was a closed project's frame, the frame and its project stay reachable from it (a heap
     * path seen in IDEA: RepaintManager, BufferStrategyPaintManager.rootJ, IdeRootPane, the frame helper,
     * the project). A person's next window paint lets go of it. With every project closed the sandbox may
     * show no window at all, so the probe paints a one-pixel window of its own and closes it.
     */
    private fun paintShowingWindows() {
        val window = JWindow().apply {
            focusableWindowState = false
            setBounds(0, 0, 1, 1)
            isVisible = true
        }
        try {
            window.rootPane.paintImmediately(0, 0, 1, 1)
        } finally {
            window.dispose()
        }
    }

    private fun recordRetained(projects: List<WeakReference<Project>>, destination: Path): Int {
        val retained = projects.count { it.get() != null }
        Files.writeString(
            destination.resolve("closed-projects.txt"),
            "$retained of ${projects.size} closed projects retained\n",
        )
        return retained
    }

    /** A disposed parent refuses a new child; one it accepts is released at once. */
    private fun disposed(panel: Disposable): Boolean {
        val probe = Disposer.newDisposable("disposal probe")
        val accepted = Disposer.tryRegister(panel, probe)
        Disposer.dispose(probe)
        return !accepted
    }
}
