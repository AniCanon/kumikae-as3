package app.anicanon.kumikae.as3.settings

internal class ArtworkChoices(paths: List<String>, current: String) {
    private val sorted = paths.sortedWith(
        compareBy<String> {
            it != current
        }.thenBy(String.CASE_INSENSITIVE_ORDER) { it },
    )

    fun search(query: String): List<String> {
        val words = query.trim().split(Regex("\\s+")).filter(String::isNotEmpty)
        if (words.isEmpty()) return sorted
        return sorted.filter { path -> words.all { path.removePrefix("res://").contains(it, ignoreCase = true) } }
    }
}
