package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.ShippedScript
import org.junit.jupiter.api.Test
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class IosSimulatorBuildTest {
    @Test fun `plugin retains the iOS build script without the legacy installer`() {
        assertTrue(ShippedScript.read("/build-ios-simulator.sh").contains("xcodebuild"))
        assertNull(ShippedScript::class.java.getResource("/install-idb.sh"))
    }

    private val root = Files.createTempDirectory("ios-build")

    private fun file(path: Path): Path {
        Files.createDirectories(path.parent)
        return Files.createFile(path)
    }

    @Test fun `the template is the one set, then the one in the home folder, then the one beside the compiler`() {
        val home = root.resolve("home")
        val compiler = file(root.resolve("repo/compiler/build/kas3c"))
        assertNull(IosSimulatorBuild.findTemplate("", home, compiler))
        val beside = file(root.resolve("repo/spike/third_party/godot-ios-template/ios-with-arm64-simulator.zip"))
        assertEquals(beside, IosSimulatorBuild.findTemplate("", home, compiler))
        val kept = file(home.resolve("Library/Application Support/Kumikae AS3/ios-with-arm64-simulator.zip"))
        assertEquals(kept, IosSimulatorBuild.findTemplate("", home, compiler))
        val set = file(root.resolve("templates/mine.zip"))
        assertEquals(set, IosSimulatorBuild.findTemplate(" $set ", home, compiler))
        assertEquals(kept, IosSimulatorBuild.findTemplate("/nowhere.zip", home, null))
    }

    @Test fun `a project can be built when it has an iOS export preset`() {
        val project = Files.createDirectories(root.resolve("game"))
        assertFalse(IosSimulatorBuild.canBuild(project))
        Files.writeString(project.resolve("export_presets.cfg"), "[preset.0]\nname=\"Android\"\nplatform=\"Android\"\n")
        assertFalse(IosSimulatorBuild.canBuild(project))
        Files.writeString(project.resolve("export_presets.cfg"), "[preset.0]\nname=\"iOS\"\nplatform=\"iOS\"\n")
        assertTrue(IosSimulatorBuild.canBuild(project))
    }

    @Test fun `the build runs the script on the project and gives back the app it names last`() {
        val project = Files.createDirectories(root.resolve("game2"))
        val template = file(root.resolve("t.zip"))
        val said = mutableListOf<String>()
        val script = "echo \"project=\$1 out=\$2 godot=\$3 template=\$4 library=\$5\"\necho /built/Game.app\n"
        val build =
            IosSimulatorBuild(project, Path.of("/tools/godot"), template, Path.of("/plugin/native/ios")) { script }
        assertEquals("/built/Game.app", build.build { text, _ -> said += text })
        assertEquals(
            "project=$project out=${project.resolve(".godot/ios-simulator")} godot=/tools/godot template=$template " +
                "library=/plugin/native/ios\n",
            said.first(),
        )
    }

    @Test fun `a build that fails says what the script said last`() {
        val project = Files.createDirectories(root.resolve("game3"))
        val build = IosSimulatorBuild(project, Path.of("/tools/godot"), file(root.resolve("u.zip")), null) {
            "echo working\necho 'The Simulator export template was not found.' >&2\nexit 1\n"
        }
        val failure = assertFailsWith<IllegalStateException> { build.build { _, _ -> } }
        assertTrue(failure.message!!.contains("The Simulator export template was not found."), failure.message)
    }
}
