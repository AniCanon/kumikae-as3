package app.anicanon.kumikae.as3.language

import org.junit.jupiter.api.Test
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class LspDescriptorTest {
    private fun descriptor(name: String) = DocumentBuilderFactory.newInstance().newDocumentBuilder()
        .parse(Path.of("src/main/resources/META-INF", name).toFile())

    @Test fun `base descriptor loads without either language client`() {
        val root = descriptor("plugin.xml")
        val dependencies = root.getElementsByTagName("depends")
        for (i in 0 until dependencies.length) {
            val dependency = dependencies.item(i) as org.w3c.dom.Element
            assertFalse(dependency.textContent == "com.intellij.modules.lsp")
            if (dependency.textContent == "com.redhat.devtools.lsp4ij") {
                assertEquals("true", dependency.getAttribute("optional"))
            }
        }
        assertEquals(0, root.getElementsByTagName("platform.lsp.integrationProvider").length)
        assertEquals(0, root.getElementsByTagName("gotoDeclarationHandler").length)
    }

    @Test fun `LSP4IJ editor handlers are not registered outside IDEA`() {
        val bridge = descriptor("as3-lsp4ij.xml")
        assertEquals(0, bridge.getElementsByTagName("codeInsight.parameterInfo").length)
        assertEquals(0, bridge.getElementsByTagName("lang.psiStructureViewFactory").length)
        val dependency = bridge.getElementsByTagName("depends").item(0) as org.w3c.dom.Element
        assertEquals("com.intellij.modules.idea", dependency.textContent)
        assertEquals("true", dependency.getAttribute("optional"))
        val idea = descriptor(dependency.getAttribute("config-file"))
        assertEquals(1, idea.getElementsByTagName("codeInsight.parameterInfo").length)
        assertEquals(1, idea.getElementsByTagName("lang.psiStructureViewFactory").length)
    }

    @Test fun `client extensions are registered in separate optional descriptors`() {
        val dependencies = descriptor("plugin.xml").getElementsByTagName("depends")
        val adapters = (0 until dependencies.length).map { dependencies.item(it) as org.w3c.dom.Element }
            .filter { it.getAttribute("optional") == "true" }
            .associate { it.textContent to it.getAttribute("config-file") }
        assertTrue("com.intellij.modules.rider" in adapters)
        assertEquals("as3-lsp4ij.xml", adapters["com.redhat.devtools.lsp4ij"])
        assertEquals(
            1,
            descriptor(adapters.getValue("com.intellij.modules.rider"))
                .getElementsByTagName("platform.lsp.integrationProvider").length,
        )
        assertEquals(
            1,
            descriptor("as3-idea-lsp4ij.xml")
                .getElementsByTagName("languageMapping").length,
        )
    }
}
