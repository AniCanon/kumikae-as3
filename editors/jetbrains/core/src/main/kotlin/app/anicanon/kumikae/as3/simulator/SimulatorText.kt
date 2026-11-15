package app.anicanon.kumikae.as3.simulator

internal object SimulatorText {
    fun validate(text: String) {
        require(text.isNotEmpty() && text.length <= 4096) { "Text must contain 1–4096 UTF-16 code units" }
        require(text.codePoints().allMatch { !Character.isISOControl(it) && it !in 0xD800..0xDFFF }) {
            "Text cannot contain control characters or unpaired surrogates"
        }
    }

    fun isAscii(text: String): Boolean = text.all { it.code in 32..126 }
}
