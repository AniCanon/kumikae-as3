package app.anicanon.kumikae.as3.project

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals

class PackageTreeTest {
    @Test fun `compact only empty single child chains`() {
        val branch = PackageFolder("supportsprint", 0, listOf(PackageFolder("config", 2), PackageFolder("entities", 3)))
        assertEquals(listOf("com", "supportsprint"), compactPackage(PackageFolder("com", 0, listOf(branch))))
        assertEquals(listOf("com"), compactPackage(PackageFolder("com", 1, listOf(branch))))
        assertEquals(listOf("assets"), compactPackage(PackageFolder("assets", 0)))
    }
}
