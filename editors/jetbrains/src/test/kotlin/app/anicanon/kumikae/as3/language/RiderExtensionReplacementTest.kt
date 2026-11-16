package app.anicanon.kumikae.as3.language

import com.intellij.openapi.Disposable
import com.intellij.openapi.extensions.ExtensionPoint
import org.junit.jupiter.api.Test
import java.lang.reflect.Proxy
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class RiderExtensionReplacementTest {
    @Test fun `failed registration leaves the native extension installed`() {
        val values = mutableListOf("native")
        val point = point(values, "registerExtension")
        assertFailsWith<IllegalStateException> {
            replaceRiderExtension(point, "native", "owned", Disposable {})
        }
        assertEquals(listOf("native"), values)
    }

    @Test fun `failed removal rolls back the replacement`() {
        val values = mutableListOf("native")
        val point = point(values, "unregisterExtension")
        assertFailsWith<IllegalStateException> {
            replaceRiderExtension(point, "native", "owned", Disposable {})
        }
        assertEquals(listOf("native"), values)
    }

    @Test fun `successful replacement retains unrelated extension instances`() {
        val values = mutableListOf("native", "other")
        replaceRiderExtension(point(values, ""), "native", "owned", Disposable {})
        assertEquals(listOf("other", "owned"), values)
    }

    @Suppress("UNCHECKED_CAST")
    private fun point(values: MutableList<String>, fail: String): ExtensionPoint<String> = Proxy.newProxyInstance(
        ExtensionPoint::class.java.classLoader,
        arrayOf(ExtensionPoint::class.java),
    ) { _, method, arguments ->
        val value = arguments?.firstOrNull() as? String
        if (method.name == fail && (value != "owned" || fail == "registerExtension")) {
            error("Registration unavailable")
        }
        when (method.name) {
            "registerExtension" -> values.add(checkNotNull(value))
            "unregisterExtension" -> values.remove(value)
        }
        null
    } as ExtensionPoint<String>
}
