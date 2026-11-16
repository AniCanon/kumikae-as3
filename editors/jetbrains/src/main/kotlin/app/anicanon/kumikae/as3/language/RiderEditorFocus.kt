package app.anicanon.kumikae.as3.language

import com.intellij.openapi.editor.event.EditorFactoryEvent
import com.intellij.openapi.editor.event.EditorFactoryListener
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Key
import java.awt.Component
import java.awt.Container
import java.awt.KeyboardFocusManager
import java.lang.ref.WeakReference

private val RELEASED_FOCUS = Key.create<ReleasedEditorFocus>("kumikae.rider.released.editor.focus")

internal class RiderEditorLifetimeListener : EditorFactoryListener {
    override fun editorReleased(event: EditorFactoryEvent) {
        if (!needsRiderLifetimeRepair()) return
        val project = event.editor.project ?: return
        val focus = project.getUserData(RELEASED_FOCUS) ?: ReleasedEditorFocus().also {
            project.putUserData(RELEASED_FOCUS, it)
        }
        focus.editorReleased(event.editor.contentComponent, isRiderProjectClosing(project))
    }
}

internal fun releaseRiderProjectFocus(project: Project) {
    project.getUserData(RELEASED_FOCUS)?.projectClosing()
}

internal class ReleasedEditorFocus {
    private var released: WeakReference<Container>? = null

    fun editorReleased(component: Component, projectClosing: Boolean) {
        val root = KeyboardFocusManager.getCurrentKeyboardFocusManager().currentFocusCycleRoot ?: return
        if (component === root || component.focusCycleRootAncestor === root) {
            released = WeakReference(root)
            if (projectClosing) this.projectClosing()
        }
    }

    fun projectClosing() {
        // Native editor release can precede the project-closing notification. Keep live focus unchanged.
        val root = released?.get()
        released = null
        val manager = KeyboardFocusManager.getCurrentKeyboardFocusManager()
        if (root != null && manager.currentFocusCycleRoot === root) manager.setGlobalCurrentFocusCycleRoot(null)
    }
}
