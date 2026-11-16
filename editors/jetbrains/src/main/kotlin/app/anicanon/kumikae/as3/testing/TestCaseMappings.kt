package app.anicanon.kumikae.as3.testing

import com.google.gson.JsonParser

internal data class TestSelection(val identity: String, val method: String?)

internal object TestCaseMappings {
    fun forFile(source: String, relative: String): List<TestSelection> {
        val manifest = JsonParser.parseString(source).asJsonObject
        require(manifest["version"].asInt == 1) { "Unsupported test manifest version" }
        return manifest.getAsJsonArray("tests").flatMap { value ->
            val test = value.asJsonObject
            val base = test["name"].asString
            val mapped = test.getAsJsonArray("cases")?.mapNotNull { entry ->
                val case = entry.asJsonObject
                if (case["source"].asString != relative) {
                    null
                } else {
                    TestSelection(
                        base + "::" + case["suite"].asString + "::" + case["name"].asString,
                        case["method"].asString,
                    )
                }
            }.orEmpty()
            if (mapped.isNotEmpty()) {
                mapped
            } else if (test["source"]?.asString == relative) {
                listOf(TestSelection(base, null))
            } else {
                emptyList()
            }
        }
    }
}
