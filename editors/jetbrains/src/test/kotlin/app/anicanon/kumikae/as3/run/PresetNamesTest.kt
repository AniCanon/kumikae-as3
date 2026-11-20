package app.anicanon.kumikae.as3.run

import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class PresetNamesTest {
    @Test fun `malformed preset files report the parse error`(@TempDir root: Path) {
        val file = root.resolve("export_presets.cfg")
        Files.writeString(file, "[preset.0]\nname=\"Web\n")
        val error = assertFailsWith<IOException> { readPresetNames(file, "Web") }
        assertTrue(error.message.orEmpty().contains("Unterminated Godot setting"))
    }
}
