package app.anicanon.kumikae.as3.project

internal object As3Names {
    private val name = Regex("[a-zA-Z_$][a-zA-Z0-9_$]*")
    private val keywords = (
        "as break case catch class const continue default delete do else extends false finally " +
            "for function if implements import in instanceof interface internal is native new null package private " +
            "protected public return super switch this throw true try typeof use var void while with"
        ).split(" ").toSet()
    fun valid(value: String) = name.matches(value) && value !in keywords
}
