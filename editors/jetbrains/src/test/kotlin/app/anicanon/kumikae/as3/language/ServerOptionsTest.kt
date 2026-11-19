package app.anicanon.kumikae.as3.language

import com.intellij.lang.Language
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.assertEquals

class ServerOptionsTest {
    @Test fun `code in a hover is marked as this plugin's language, which the IDE knows by that name`() {
        assertEquals(mapOf("codeLanguage" to "KumikaeAS3"), serverOptions())
        assertEquals(As3Language, Language.findLanguageByID(serverOptions().getValue("codeLanguage")))
    }

    @Test fun `bundled test declarations include both cli and godot testing libraries`(@TempDir temp: Path) {
        val nativeRoot = temp.resolve("native")
        val as3Dir = Files.createDirectories(nativeRoot.resolve("testing/as3/app/anicanon/kumikae/testing"))
        val godotDir = Files.createDirectories(nativeRoot.resolve("testing/godot/app/anicanon/kumikae/testing"))
        val assertFile = Files.writeString(as3Dir.resolve("Assert.as"), "package {}")
        val sceneFile = Files.writeString(godotDir.resolve("TestScene.as"), "package {}")
        val projectRoot = Files.createDirectories(temp.resolve("project"))

        assertEquals(listOf(assertFile, sceneFile), bundledTestDeclarations(nativeRoot, projectRoot))

        val frameworkFile = Files.createDirectories(projectRoot.resolve("tests/framework/app/anicanon/kumikae/testing"))
        Files.writeString(frameworkFile.resolve("TestCase.as"), "package {}")
        assertEquals(emptyList(), bundledTestDeclarations(nativeRoot, projectRoot))
    }
}
