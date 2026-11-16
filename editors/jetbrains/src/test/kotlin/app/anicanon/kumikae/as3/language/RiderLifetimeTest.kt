package app.anicanon.kumikae.as3.language

import com.intellij.analysis.problemsView.toolWindow.ProblemsViewPanelProvider
import com.intellij.analysis.problemsView.toolWindow.ProblemsViewTab
import com.intellij.openapi.Disposable
import com.intellij.openapi.util.Disposer
import org.junit.jupiter.api.Test
import java.awt.KeyboardFocusManager
import javax.swing.JPanel
import javax.swing.SwingUtilities
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertSame

class RiderLifetimeTest {
    @Test fun `released editor clears only its own focus root`() {
        SwingUtilities.invokeAndWait {
            val focus = ReleasedEditorFocus()
            val focusManager = KeyboardFocusManager.getCurrentKeyboardFocusManager()
            val previous = focusManager.currentFocusCycleRoot
            val root = JPanel().apply { isFocusCycleRoot = true }
            val editor = JPanel()
            root.add(editor)
            try {
                focusManager.setGlobalCurrentFocusCycleRoot(root)
                focus.editorReleased(JPanel(), true)
                assertSame(root, focusManager.currentFocusCycleRoot)
                focus.editorReleased(editor, true)
                assertNull(focusManager.currentFocusCycleRoot)
            } finally {
                focusManager.setGlobalCurrentFocusCycleRoot(previous)
            }
        }
    }

    @Test fun `releasing an inactive editor preserves the enclosing window focus root`() {
        SwingUtilities.invokeAndWait {
            val focus = ReleasedEditorFocus()
            val manager = KeyboardFocusManager.getCurrentKeyboardFocusManager()
            val previous = manager.currentFocusCycleRoot
            val window = JPanel().apply { isFocusCycleRoot = true }
            val editorRoot = JPanel().apply { isFocusCycleRoot = true }
            val editor = JPanel()
            window.add(editorRoot)
            editorRoot.add(editor)
            try {
                manager.setGlobalCurrentFocusCycleRoot(window)
                focus.editorReleased(editor, true)
                assertSame(window, manager.currentFocusCycleRoot)
            } finally {
                manager.setGlobalCurrentFocusCycleRoot(previous)
            }
        }
    }

    @Test fun `releasing an editor in a live project preserves its focus root`() {
        SwingUtilities.invokeAndWait {
            val focus = ReleasedEditorFocus()
            val manager = KeyboardFocusManager.getCurrentKeyboardFocusManager()
            val previous = manager.currentFocusCycleRoot
            val root = JPanel().apply { isFocusCycleRoot = true }
            val editor = JPanel()
            root.add(editor)
            try {
                manager.setGlobalCurrentFocusCycleRoot(root)
                focus.editorReleased(editor, false)
                assertSame(root, manager.currentFocusCycleRoot)
                focus.projectClosing()
                assertNull(manager.currentFocusCycleRoot)
            } finally {
                manager.setGlobalCurrentFocusCycleRoot(previous)
            }
        }
    }

    private class Panel :
        ProblemsViewTab,
        Disposable {
        var disposals = 0
        override fun getName(count: Int) = "Current file"
        override fun getTabId() = "current"
        override fun dispose() {
            disposals++
        }
    }

    @Test fun `replaced native panel is disposed with its tool window`() {
        val owner = Disposer.newDisposable()
        val panel = Panel()
        val provider = RiderOwnedPanelProvider(
            { owner },
            object : ProblemsViewPanelProvider {
                override fun create() = panel
            },
        )
        try {
            assertSame(panel, provider.create())
            assertEquals(0, panel.disposals)
        } finally {
            Disposer.dispose(owner)
        }
        assertEquals(1, panel.disposals)
    }

    @Test fun `provider without a panel keeps native behavior`() {
        val provider = RiderOwnedPanelProvider(
            { Disposable {} },
            object : ProblemsViewPanelProvider {
                override fun create(): ProblemsViewTab? = null
            },
        )
        assertNull(provider.create())
    }

    @Test fun `disposed owner cannot orphan a newly created panel`() {
        val owner = Disposer.newDisposable()
        Disposer.dispose(owner)
        val panel = Panel()
        val provider = RiderOwnedPanelProvider(
            { owner },
            object : ProblemsViewPanelProvider {
                override fun create() = panel
            },
        )
        assertNull(provider.create())
        assertEquals(1, panel.disposals)
    }

    @Test fun `missing owner prevents native panel allocation`() {
        var created = 0
        val provider = RiderOwnedPanelProvider(
            { error("Tool window is missing") },
            object : ProblemsViewPanelProvider {
                override fun create() = Panel().also { created++ }
            },
        )
        assertNull(provider.create())
        assertEquals(0, created)
    }
}
