package app.anicanon.kumikae.as3.simulator

import org.junit.Test
import java.nio.file.Files
import kotlin.test.assertFalse

class NativeOnlySessionTest {
    @Test fun missingNativeHelperNeverEnablesAnInstalledLegacyBackend() {
        val parent = Files.createTempDirectory("native-only-session")
        try {
            LocalSession(parent, parent.resolve("missing-helper")).use {
                assertFalse(it.enableInput(), "Missing native helper must not enable idb")
            }
        } finally {
            Files.delete(parent)
        }
    }
}
