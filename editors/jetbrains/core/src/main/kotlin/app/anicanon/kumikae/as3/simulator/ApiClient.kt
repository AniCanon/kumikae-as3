package app.anicanon.kumikae.as3.simulator

import com.google.gson.Gson
import com.google.gson.JsonElement
import com.google.gson.JsonParser
import java.net.HttpURLConnection
import java.net.Proxy
import java.net.URI
import java.util.UUID

class ApiClient(private val endpoint: Endpoint) {
    fun call(method: String, params: Map<String, Any> = emptyMap()): JsonElement {
        val id = UUID.randomUUID().toString()
        val payload = Gson().toJson(mapOf("version" to 1, "id" to id, "method" to method, "params" to params))
        val connection = URI(
            "http://127.0.0.1:${endpoint.port}/v1",
        ).toURL().openConnection(Proxy.NO_PROXY) as HttpURLConnection
        try {
            connection.connectTimeout = 2000
            connection.readTimeout = 130_000
            connection.instanceFollowRedirects = false
            connection.requestMethod = "POST"
            connection.setRequestProperty("Authorization", "Bearer ${endpoint.token}")
            connection.setRequestProperty("Content-Type", "application/json")
            connection.doOutput = true
            connection.outputStream.use { it.write(payload.toByteArray(Charsets.UTF_8)) }
            if (connection.responseCode != 200) {
                throw SimulatorException("HTTP_ERROR", "Local service returned HTTP ${connection.responseCode}")
            }
            val response = connection.inputStream.reader(Charsets.UTF_8).use { JsonParser.parseReader(it).asJsonObject }
            if (response["id"]?.asString != id || response["version"]?.asInt != 1) {
                throw SimulatorException("PROTOCOL_ERROR", "Wrong response id or version")
            }
            response.getAsJsonObject("error")?.let {
                throw SimulatorException(it["code"].asString, it["message"].asString)
            }
            return response["result"] ?: throw SimulatorException("PROTOCOL_ERROR", "Missing result")
        } finally {
            connection.disconnect()
        }
    }
}
