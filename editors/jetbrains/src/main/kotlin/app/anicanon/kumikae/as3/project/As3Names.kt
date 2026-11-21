package app.anicanon.kumikae.as3.project

internal object As3Names {
    private val name = Regex("[a-zA-Z_$][a-zA-Z0-9_$]*")
    private val keywords = checkNotNull(As3Names::class.java.getResourceAsStream("/as3-reserved-words.txt")) {
        "Missing compiler reserved words in the AS3 plugin."
    }.bufferedReader().useLines { it.toSet() }

    fun valid(value: String) = name.matches(value) && value !in keywords
}
