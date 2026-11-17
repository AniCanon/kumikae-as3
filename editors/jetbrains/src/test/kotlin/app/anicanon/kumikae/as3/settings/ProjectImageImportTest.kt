package app.anicanon.kumikae.as3.settings

import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class ProjectImageImportTest {
    @TempDir lateinit var folder: Path

    @Test fun `import keeps existing artwork and chooses a distinct resource name`() {
        val project = Files.createDirectory(folder.resolve("game"))
        val assets = Files.createDirectory(project.resolve("assets"))
        Files.writeString(assets.resolve("title.png"), "old")
        val source = Files.writeString(folder.resolve("title.png"), "new")
        val result = ProjectImageImport.copy(source, project)
        assertEquals("res://assets/title-2.png", result)
        assertEquals("old", Files.readString(assets.resolve("title.png")))
        assertEquals("new", Files.readString(assets.resolve("title-2.png")))
    }

    @Test fun `a linked assets directory assigns the visible resource under its canonical path`() {
        val project = Files.createDirectory(folder.resolve("game"))
        val art = Files.createDirectory(project.resolve("art"))
        try {
            Files.createSymbolicLink(project.resolve("assets"), art)
        } catch (unsupported: java.nio.file.FileSystemException) {
            org.junit.jupiter.api.Assumptions.assumeTrue(false, unsupported.reason)
        } catch (_: UnsupportedOperationException) {
            org.junit.jupiter.api.Assumptions.assumeTrue(false, "Filesystem does not support symbolic links")
        }
        val source = Files.writeString(folder.resolve("title.png"), "new")
        assertEquals("res://art/title.png", ProjectImageImport.copy(source, project))
    }

    @Test fun `an ignored assets folder cannot receive an unusable import`() {
        val project = Files.createDirectory(folder.resolve("game"))
        val assets = Files.createDirectory(project.resolve("assets"))
        Files.createFile(assets.resolve(".gdignore"))
        val source = Files.writeString(folder.resolve("title.png"), "new")
        assertFailsWith<IllegalArgumentException> { ProjectImageImport.copy(source, project) }
        assertEquals(false, Files.exists(assets.resolve("title.png")))
    }
}
