package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonObject

internal object InputArguments {
    fun prepare(method: String, p: JsonObject, geometry: FrameGeometry, scale: Double): List<String> = when (method) {
        "tap", "swipe" -> gesture(method, p, geometry, scale)

        "text" -> listOf(
            p["text"].asString.also { text ->
                SimulatorText.validate(text)
            },
        )

        "button" -> listOf(p["button"].asString.also { require(it in setOf("home", "lock")) }.uppercase())

        "rotate" -> listOf(
            p["orientation"].asString.also {
                require(it in setOf("portrait", "portrait_upside_down", "landscape_left", "landscape_right"))
            }.uppercase(),
        )

        else -> error("Unknown input operation")
    }

    private fun gesture(method: String, p: JsonObject, geometry: FrameGeometry, scale: Double): List<String> {
        fun dimension(name: String) = p[name].asLong.also {
            require(it in 1..Int.MAX_VALUE.toLong()) { "Invalid frame dimension" }
        }.toInt()
        val displayed = Geometry(dimension("width"), dimension("height"))
        val stamp = p["generation"].asLong
        val start = geometry.point(stamp, displayed, p["x"].asDouble, p["y"].asDouble, scale)
        if (method == "tap") return listOf(start.x.toString(), start.y.toString())
        val end = geometry.point(stamp, displayed, p["endX"].asDouble, p["endY"].asDouble, scale)
        val duration = p["durationMs"].asLong.also { require(it in 50..5000) { "Swipe duration must be 50–5000ms" } }
        return listOf(
            start.x.toString(),
            start.y.toString(),
            end.x.toString(),
            end.y.toString(),
            "--duration",
            (duration / 1000.0).toString(),
        )
    }
}
