package app.anicanon.kumikae.as3.settings

internal enum class GameResourceKind(val title: String, private vararg val types: String) {
    Scene("Main scene", "tscn", "scn"),
    Icon("Application icon", "png", "svg", "webp", "jpg", "jpeg"),
    Splash("Boot splash", "png"),
    ExportIcon("Platform icon override", "png"),
    ;

    fun accepts(name: String) = name.substringAfterLast('.', "").lowercase() in types
}

internal fun viewportRatio(width: Int, height: Int): String {
    require(width > 0 && height > 0)
    tailrec fun gcd(a: Int, b: Int): Int = if (b == 0) a else gcd(b, a % b)
    val divisor = gcd(width, height)
    return "${width / divisor}:${height / divisor}"
}
