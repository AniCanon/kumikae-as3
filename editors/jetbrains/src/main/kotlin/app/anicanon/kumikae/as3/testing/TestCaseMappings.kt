package app.anicanon.kumikae.as3.testing

import com.google.gson.JsonParser

internal data class TestSelection(val identity: String, val method: String?)

internal object TestCaseMappings {
    /**
     * The cases a place in a file selects: its method's own, or the file's where it is in no method. A
     * method with no case of its own (a helper) selects none, unless the file is registered whole.
     */
    fun at(cases: List<TestSelection>, method: String?): List<TestSelection> {
        if (method == null) return cases
        return cases.filter { it.method == method }.ifEmpty { cases.filter { it.method == null } }
    }

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
