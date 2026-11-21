package app.anicanon.kumikae.as3.scene

import com.intellij.openapi.progress.ProcessCanceledException
import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNull
import kotlin.test.assertTrue

class SceneControllerTest {
    @Test fun `reload selects root node and attach updates selection and status`() {
        val initial = "[gd_scene format=3]\n\n[node name=\"Root\" type=\"Node2D\"]\n"
        var current = initial
        val repository = object : SceneRepository {
            override fun read() = current
            override fun attach(expected: String, node: SceneNode, path: String?): String {
                assertEquals(current, expected)
                current = SceneDocument(expected).attach(node.path, path)
                return current
            }
            override fun open(path: String) = Unit
        }
        val controller = SceneController(repository)
        controller.reload()
        assertEquals(1, controller.nodes.size)
        assertEquals("Root", controller.selected?.name)
        controller.attach("res://Player.as")
        assertEquals("res://Player.as", controller.selected?.script)
        assertEquals("Script attached.", controller.status)
        controller.attach(null)
        assertNull(controller.selected?.script)
        assertEquals("Script detached.", controller.status)
    }

    @Test fun `process cancellation is rethrown while regular errors update status`() {
        var throwCancel = false
        val repository = object : SceneRepository {
            override fun read(): String {
                if (throwCancel) throw ProcessCanceledException()
                error("Corrupt scene")
            }
            override fun attach(expected: String, node: SceneNode, path: String?) = expected
            override fun open(path: String) = Unit
        }
        val controller = SceneController(repository)
        controller.reload()
        assertTrue(controller.status.contains("Corrupt scene"))
        throwCancel = true
        assertFailsWith<ProcessCanceledException> { controller.reload() }
    }
}
