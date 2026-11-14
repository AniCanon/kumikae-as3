package app.anicanon.kumikae.as3.simulator

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import org.junit.Assert.*
import org.junit.Test

class ProtocolTest {
    private val protocol = Protocol { method, params -> mapOf("method" to method, "params" to params) }
    private fun response(text: String) = JsonParser.parseString(protocol.respond(text)).asJsonObject

    @Test fun `request id and result survive a round trip`() {
        val result = response("""{"version":1,"id":"a","method":"devices","params":{}}""")
        assertEquals("a", result["id"].asString)
        assertEquals(1, result["version"].asInt)
        assertEquals("devices", result.getAsJsonObject("result")["method"].asString)
    }

    @Test fun `malformed and ambiguous requests never dispatch`() {
        var called = false
        val p = Protocol { _, _ -> called = true; JsonObject() }
        val bad = listOf("", "[]", "{}", "null", "{broken", "{\"version\":1,}",
            """{"version":1,"version":1,"id":"a","method":"devices","params":{}}""",
            """{"version":"1","id":"a","method":"devices","params":{}}""",
            """{"version":2,"id":"a","method":"devices","params":{}}""",
            """{"version":1,"id":"a","method":"devices","params":{},"extra":1}""",
            """{"version":1,"id":"a","method":"unknown","params":{}}""",
            """{"version":1,"id":"a","method":"devices","params":{"extra":1}}""",
            """{"version":1,"id":"a","method":"select","params":{"device":3}}""",
            """{"version":1,"id":"a","method":"tap","params":{"device":"d","x":"2"}}""")
        for (text in bad) {
            assertEquals(text, "BAD_REQUEST", JsonParser.parseString(p.respond(text)).asJsonObject
                .getAsJsonObject("error")["code"].asString)
        }
        assertFalse(called)
    }

    @Test fun `known service failures retain their code`() {
        val p = Protocol { _, _ -> throw SimulatorException("INPUT_DISABLED", "Enable input first") }
        val result = JsonParser.parseString(p.respond("""{"version":1,"id":"a","method":"devices","params":{}}""")).asJsonObject
        assertEquals("a", result["id"].asString)
        assertEquals("INPUT_DISABLED", result.getAsJsonObject("error")["code"].asString)
    }

    @Test fun `every method has an explicit parameter contract`() {
        val cases = mapOf(
            "devices" to "{}", "status" to "{}", "select" to """{"device":"d"}""",
            "boot" to """{"device":"d"}""", "screenshot" to """{"device":"d","format":"png"}""",
            "tap" to """{"device":"d","generation":1,"width":100,"height":200,"x":2,"y":3}""",
            "swipe" to """{"device":"d","generation":1,"width":100,"height":200,"x":2,"y":3,"endX":4,"endY":5,"durationMs":500}""",
            "text" to """{"device":"d","text":"hello"}""",
            "button" to """{"device":"d","button":"home"}""",
            "rotate" to """{"device":"d","orientation":"portrait"}""",
            "install" to """{"device":"d","path":"/tmp/a.app"}""",
            "launch" to """{"device":"d","bundle":"org.kumikae.as3test"}""",
            "report" to """{"device":"d","bundle":"org.kumikae.as3test"}""",
            "log" to """{"cursor":0}"""
        )
        cases.forEach { (method, params) ->
            val result = response("""{"version":1,"id":"a","method":"$method","params":$params}""")
            assertFalse(result.toString(), result.has("error"))
        }
    }
}
