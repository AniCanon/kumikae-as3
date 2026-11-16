package app.anicanon.kumikae.as3.language

import com.redhat.devtools.lsp4ij.client.features.LSPClientFeatures
import com.redhat.devtools.lsp4ij.server.DefaultLauncherBuilder
import org.eclipse.lsp4j.DidChangeTextDocumentParams
import org.eclipse.lsp4j.DidCloseTextDocumentParams
import org.eclipse.lsp4j.DidOpenTextDocumentParams
import org.eclipse.lsp4j.jsonrpc.MessageConsumer
import org.eclipse.lsp4j.jsonrpc.messages.Message
import org.eclipse.lsp4j.jsonrpc.messages.NotificationMessage
import org.eclipse.lsp4j.services.LanguageServer
import java.util.concurrent.ConcurrentHashMap

/**
 * The version of each open document last sent to the server, read from the messages themselves: LSP4IJ
 * keeps its own count to itself. A publication for another version is not for the text LSP4IJ has.
 */
internal class SentVersions {
    private val sent = ConcurrentHashMap<String, Int>()

    fun record(message: Message) {
        if (message !is NotificationMessage) return
        when (val params = message.params) {
            is DidOpenTextDocumentParams -> sent[params.textDocument.uri] = params.textDocument.version
            is DidChangeTextDocumentParams -> sent[params.textDocument.uri] = params.textDocument.version
            is DidCloseTextDocumentParams -> sent.remove(params.textDocument.uri)
        }
    }

    fun current(uri: String, version: Int?): Boolean = version == null || sent[uri]?.let { it == version } ?: true
}

/** LSP4IJ's own launcher, with every message the client sends shown to [versions] on its way out. */
internal class VersionTrackingLauncherBuilder<S : LanguageServer>(
    features: LSPClientFeatures,
    private val versions: SentVersions,
) : DefaultLauncherBuilder<S>(features) {
    override fun wrapMessageConsumer(consumer: MessageConsumer): MessageConsumer {
        val wrapped = super.wrapMessageConsumer(consumer)
        return MessageConsumer { message ->
            versions.record(message)
            wrapped.consume(message)
        }
    }
}
