package org.kumikae.simulator

import com.google.gson.JsonElement
import com.google.gson.JsonObject

object RequestFields {
    private val device = "device:s"
    private val geometry = "$device generation:i width:i height:i x:n y:n"
    private val fields = mapOf(
        "devices" to "", "status" to "", "select" to device, "boot" to device,
        "screenshot" to "$device format?:s", "tap" to geometry,
        "swipe" to "$geometry endX:n endY:n durationMs:i", "text" to "$device text:s",
        "button" to "$device button:s", "rotate" to "$device orientation:s",
        "install" to "$device path:s", "launch" to "$device bundle:s",
        "report" to "$device bundle:s", "log" to "cursor:i"
    ).mapValues { (_, spec) -> spec.split(' ').filter(String::isNotEmpty).map { it.split(':') } }

    fun validate(method: String, params: JsonObject) {
        val schema = fields[method] ?: throw IllegalArgumentException("Unknown method: $method")
        val allowed = schema.map { it[0].removeSuffix("?") }.toSet()
        require(params.keySet().all { it in allowed }) { "Unknown parameter" }
        schema.forEach { (field, kind) ->
            val name = field.removeSuffix("?")
            if (!params.has(name) && field.endsWith('?')) return@forEach
            val value = params[name] ?: throw IllegalArgumentException("Missing $name")
            when (kind) {
                "s" -> require(string(value).length <= 4096) { "$name is too long" }
                "i" -> integer(value)
                "n" -> number(value)
            }
        }
    }

    fun string(value: JsonElement): String {
        require(value.isJsonPrimitive && value.asJsonPrimitive.isString) { "Expected a string" }
        return value.asString.also { require('\u0000' !in it) { "NUL is not allowed" } }
    }

    fun number(value: JsonElement): Double {
        require(value.isJsonPrimitive && value.asJsonPrimitive.isNumber) { "Expected a number" }
        return value.asDouble.also { require(it.isFinite()) { "Number must be finite" } }
    }

    fun integer(value: JsonElement): Long {
        number(value)
        return try { value.asBigDecimal.longValueExact() }
        catch (e: ArithmeticException) { throw IllegalArgumentException("Expected a 64-bit integer") }
    }
}
