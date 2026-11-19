package app.anicanon.kumikae.as3.scene

import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberUpdatedState
import com.intellij.icons.AllIcons
import com.intellij.ui.ColoredTreeCellRenderer
import com.intellij.ui.SimpleTextAttributes
import com.intellij.ui.components.JBScrollPane
import com.intellij.ui.treeStructure.Tree
import org.jetbrains.compose.swing.modifier.SwingModifier
import org.jetbrains.compose.swing.node.SwingNode
import javax.swing.JTree
import javax.swing.tree.DefaultMutableTreeNode
import javax.swing.tree.DefaultTreeModel
import javax.swing.tree.TreePath

@Composable
internal fun SceneTree(
    nodes: List<SceneNode>,
    selected: SceneNode?,
    onSelect: (SceneNode) -> Unit,
    modifier: SwingModifier = SwingModifier,
) {
    val selectCallback = rememberUpdatedState(onSelect)
    SwingNode(
        factory = { SceneTreePane { selectCallback.value(it) } },
        modifier = modifier,
        update = {
            set(nodes) { updateNodes(it) }
            set(selected) { syncSelection(it) }
        },
    )
}

private class SceneTreePane(private val onSelect: (SceneNode) -> Unit) : JBScrollPane() {
    private val model = DefaultTreeModel(DefaultMutableTreeNode())
    private val tree = Tree(model)
    private var byPath: Map<String, DefaultMutableTreeNode> = emptyMap()
    private var syncing = false

    init {
        tree.isRootVisible = false
        tree.showsRootHandles = true
        tree.cellRenderer = object : ColoredTreeCellRenderer() {
            override fun customizeCellRenderer(
                tree: JTree,
                value: Any?,
                selected: Boolean,
                expanded: Boolean,
                leaf: Boolean,
                row: Int,
                focus: Boolean,
            ) {
                val node = (value as? DefaultMutableTreeNode)?.userObject as? SceneNode ?: return
                icon = if (node.script != null) AllIcons.Nodes.Class else AllIcons.Nodes.Folder
                append(node.name)
                append("   " + node.type.ifEmpty { "Instance" }, SimpleTextAttributes.GRAYED_ATTRIBUTES)
            }
        }
        tree.addTreeSelectionListener {
            if (syncing) return@addTreeSelectionListener
            val node = (tree.lastSelectedPathComponent as? DefaultMutableTreeNode)?.userObject as? SceneNode
            if (node != null) onSelect(node)
        }
        setViewportView(tree)
    }

    fun updateNodes(nodes: List<SceneNode>) {
        syncing = true
        try {
            val root = DefaultMutableTreeNode()
            val mapped = nodes.associate { it.path to DefaultMutableTreeNode(it) }
            for (node in nodes) {
                val parent = if (node.path == ".") root else mapped[node.path.substringBeforeLast('/', ".")] ?: root
                parent.add(mapped.getValue(node.path))
            }
            byPath = mapped
            model.setRoot(root)
            tree.expandRow(0)
        } finally {
            syncing = false
        }
    }

    fun syncSelection(node: SceneNode?) {
        val target = node?.let { byPath[it.path] }?.let { TreePath(it.path) }
        if (tree.selectionPath == target) return
        syncing = true
        try {
            tree.selectionPath = target
        } finally {
            syncing = false
        }
    }
}
