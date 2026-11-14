package app.anicanon.kumikae.as3.simulator

import com.google.gson.*
import com.google.gson.stream.JsonReader
import com.google.gson.stream.JsonToken
import java.io.StringReader
import java.math.BigDecimal

class SimulatorException(val code: String, message: String) : RuntimeException(message)

class Protocol(private val dispatch: (String, JsonObject) -> Any) {
    private val gson = GsonBuilder().serializeNulls().create()

    fun respond(text: String): String {
        var id: String? = null
        return try {
            require(text.length <= MAX_REQUEST) { "Request is too large" }
            val reader = JsonReader(StringReader(text)).apply { strictness = Strictness.STRICT }
            val request = reader.use {
                val value = readValue(it, 0)
                require(it.peek() == JsonToken.END_DOCUMENT) { "Trailing JSON" }
                require(value.isJsonObject) { "Request must be an object" }
                value.asJsonObject
            }
            require(request.keySet() == setOf("version", "id", "method", "params")) { "Wrong request fields" }
            require(RequestFields.integer(request["version"]) == 1L) { "Unsupported protocol version" }
            id = RequestFields.string(request["id"]).also { require(it.isNotEmpty() && it.length <= 128) }
            val method = RequestFields.string(request["method"])
            require(request["params"].isJsonObject) { "params must be an object" }
            val params = request.getAsJsonObject("params")
            RequestFields.validate(method, params)
            gson.toJson(mapOf("version" to 1, "id" to id, "result" to dispatch(method, params)))
        } catch (e: SimulatorException) {
            error(id, e.code, e.message ?: e.code)
        } catch (e: IllegalArgumentException) {
            error(id, "BAD_REQUEST", e.message ?: "Invalid request")
        } catch (e: JsonParseException) {
            error(id, "BAD_REQUEST", "Invalid JSON")
        } catch (e: java.io.IOException) {
            error(id, "BAD_REQUEST", "Invalid JSON")
        } catch (e: Exception) {
            error(id, "INTERNAL_ERROR", "Operation failed: ${e.javaClass.simpleName}")
        }
    }

    private fun error(id: String?, code: String, message: String) = gson.toJson(
        mapOf("version" to 1, "id" to id, "error" to mapOf("code" to code, "message" to message)))

    private fun readValue(reader: JsonReader, depth: Int): JsonElement {
        require(depth <= 8) { "JSON is nested too deeply" }
        return when (reader.peek()) {
            JsonToken.BEGIN_OBJECT -> JsonObject().also { obj ->
                reader.beginObject()
                while (reader.hasNext()) {
                    val name = reader.nextName()
                    require(!obj.has(name)) { "Duplicate JSON field" }
                    obj.add(name, readValue(reader, depth + 1))
                }
                reader.endObject()
            }
            JsonToken.BEGIN_ARRAY -> JsonArray().also { array ->
                reader.beginArray()
                while (reader.hasNext()) array.add(readValue(reader, depth + 1))
                reader.endArray()
            }
            JsonToken.STRING -> JsonPrimitive(reader.nextString())
            JsonToken.NUMBER -> JsonPrimitive(BigDecimal(reader.nextString()))
            JsonToken.BOOLEAN -> JsonPrimitive(reader.nextBoolean())
            JsonToken.NULL -> { reader.nextNull(); JsonNull.INSTANCE }
            else -> throw IllegalArgumentException("Invalid JSON value")
        }
    }

    companion object { const val MAX_REQUEST = 65536 }
}
