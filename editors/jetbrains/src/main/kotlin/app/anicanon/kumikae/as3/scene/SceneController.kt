package app.anicanon.kumikae.as3.scene

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

internal interface SceneRepository {
    fun read(): String
    fun attach(expected: String, node: SceneNode, path: String?): String
    fun openInGodot() = Unit
    fun open(path: String)
}

internal class SceneController(private val repository: SceneRepository) {
    private var source = ""
    var nodes by mutableStateOf<List<SceneNode>>(emptyList())
        private set
    var selected by mutableStateOf<SceneNode?>(null)
    var status by mutableStateOf("")
        private set

    fun reload() = attempt {
        source = repository.read()
        nodes = SceneDocument(source).nodes
        selected = nodes.firstOrNull { it.path == selected?.path } ?: nodes.firstOrNull()
        status = "Select a node to inspect its AS3 behavior."
    }

    fun attach(path: String?) = attempt {
        val node = requireNotNull(selected) { "Select a node first." }
        source = repository.attach(source, node, path)
        nodes = SceneDocument(source).nodes
        selected = nodes.firstOrNull { it.path == node.path }
        status = if (path == null) "Script detached." else "Script attached."
    }

    fun openInGodot() = attempt { repository.openInGodot() }
    fun open() = attempt { selected?.script?.let(repository::open) }

    private fun attempt(action: () -> Unit) {
        runCatching(action).onFailure { status = it.message ?: "Scene operation failed." }
    }
}
