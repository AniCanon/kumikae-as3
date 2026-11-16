package app.anicanon.kumikae.as3.testing

import org.junit.Assert.assertEquals
import org.junit.Test

class TestCaseMappingsTest {
    private val manifest = """{"version":1,"tests":[{"name":"Models","source":"All.as","cases":[
        {"source":"Course.as","suite":"Course","name":"gap jump","method":"gapJump"},
        {"source":"Course.as","suite":"Course","name":"magnet","method":"magnet"},
        {"source":"Input.as","suite":"Input","name":"tap","method":"tap"}]}]}"""

    @Test fun fileSelectsEveryMappedCaseAndEntrySelectsWholeTarget() {
        assertEquals(
            listOf("Models::Course::gap jump", "Models::Course::magnet"),
            TestCaseMappings.forFile(manifest, "Course.as").map { it.identity },
        )
        assertEquals(listOf(TestSelection("Models", null)), TestCaseMappings.forFile(manifest, "All.as"))
        assertEquals(emptyList<TestSelection>(), TestCaseMappings.forFile(manifest, "Helper.as"))
    }

    @Test fun entryFileStillExposesItsIndividualCases() {
        val direct = manifest.replace("All.as", "Course.as")
        assertEquals(listOf("gapJump", "magnet"), TestCaseMappings.forFile(direct, "Course.as").map { it.method })
    }
}
