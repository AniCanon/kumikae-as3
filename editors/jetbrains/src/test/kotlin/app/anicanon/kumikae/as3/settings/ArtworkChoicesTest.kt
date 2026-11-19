package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals

class ArtworkChoicesTest {
    @Test fun `vector dimensions come from the source even for a bounded preview`() {
        fun size(attributes: String) = VectorArtwork.size("<svg $attributes/>".byteInputStream())
        val illustrator = """<?xml version="1.0"?>
            <!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">
            <svg width="32" height="24"/>
        """.trimIndent()
        assertEquals(32 to 24, VectorArtwork.size(illustrator.byteInputStream()))
        assertEquals(64000 to 8000, size("width='64000' height='8000'"))
        assertEquals(480 to 120, size("viewBox='0 0 480 120'"))
        assertEquals(192 to 96, size("width='2in' viewBox='0 0 60 30'"))
    }

    @Test fun `Godot icon inputs are assignable even when the IDE has no image reader`() {
        for (ext in listOf("bmp", "hdr", "exr", "tga", "webp")) {
            assertEquals(true, GameResourceKind.Icon.accepts("icon.$ext"))
            assertEquals(false, GameResourceKind.Splash.accepts("icon.$ext"))
        }
    }

    private val gold = "res://ui/title.png"
    private val other = "res://menu/title.png"
    private val vector = "res://ui/icon.svg"

    @Test fun `choices rank the current asset and search across folder and file`() {
        val choices = ArtworkChoices(listOf(other, vector, gold), gold)
        assertEquals(listOf(gold, other, vector), choices.search(""))
        assertEquals(listOf(gold), choices.search("TITLE  ui"))
        assertEquals(emptyList(), choices.search("missing"))
        assertEquals(listOf(gold, other), choices.search("title"))
    }
}
