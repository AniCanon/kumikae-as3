package app.anicanon.kumikae.as3.scene

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class SceneDocumentTest {
    private val scene = """
        [gd_scene load_steps=2 format=3]
        [ext_resource type="Script" path="res://App.as" id="app"]
        [node name="Game" type="Node2D"]
        script = ExtResource("app")
        position = Vector2(1, 2)
        [node name="Sound" type="AudioStreamPlayer" parent="."]
    """.trimIndent()

    @Test fun `attachments describe their actual nodes`() {
        val nodes = SceneDocument(scene).nodes
        assertEquals("res://App.as", nodes[0].script)
        assertEquals("Sound", nodes[1].path)
        assertEquals("AudioStreamPlayer", nodes[1].type)
    }

    @Test fun `attach and detach preserve unrelated properties`() {
        val changed = SceneDocument(scene).attach("Sound", "res://Sound.as")
        assertEquals("res://Sound.as", SceneDocument(changed).nodes[1].script)
        assertTrue(changed.contains("position = Vector2(1, 2)"))
        assertEquals(null, SceneDocument(SceneDocument(changed).attach("Sound", null)).nodes[1].script)
    }

    @Test fun `instances and resource traversal cannot be edited blindly`() {
        assertFailsWith<IllegalArgumentException> { SceneDocument(scene).attach("Sound", "res://../Other.as") }
        val inherited = "[gd_scene format=3]\n[node name=\"Game\" instance=ExtResource(\"base\")]\n"
        assertFailsWith<IllegalArgumentException> { SceneDocument(inherited).attach(".", "res://App.as") }
    }
}
