package app.anicanon.kumikae.as3.language

import com.intellij.execution.configurations.GeneralCommandLine
import java.nio.file.Path

/** Shared wire options do not load either IDE's language-client classes. */
internal fun serverOptions(): Map<String, String> = mapOf("codeLanguage" to As3Language.id)

internal fun serverCommandLine(compiler: Path): GeneralCommandLine {
    val trace = System.getenv("KUMIKAE_AS3_LSP_TRACE")?.takeIf { it.isNotBlank() }
        ?: return GeneralCommandLine(compiler.toString(), "lsp")
    val script = "tee \"\$1/to-server.log\" | \"\$2\" lsp | tee \"\$1/from-server.log\""
    return GeneralCommandLine("/bin/sh", "-c", script, "sh", trace, compiler.toString())
}
