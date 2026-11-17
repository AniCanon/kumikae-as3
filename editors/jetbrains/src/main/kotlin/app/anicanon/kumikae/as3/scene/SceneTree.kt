package app.anicanon.kumikae.as3.scene

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import app.anicanon.kumikae.as3.ui.Native
import com.intellij.icons.AllIcons
import com.intellij.ui.ColoredTreeCellRenderer
import com.intellij.ui.SimpleTextAttributes
import com.intellij.ui.components.JBScrollPane
import com.intellij.ui.treeStructure.Tree
import org.jetbrains.compose.swing.modifier.SwingModifier
import javax.swing.JTree
import javax.swing.tree.DefaultMutableTreeNode
import javax.swing.tree.DefaultTreeModel
import javax.swing.tree.TreePath

@Composable
internal fun SceneTree(controller: SceneController, modifier: SwingModifier) {
    val nodes = controller.nodes
    val component = remember(nodes) {
        val root = DefaultMutableTreeNode()
        val byPath = nodes.associate { it.path to DefaultMutableTreeNode(it) }
        for (node in nodes) {
            val parent = if (node.path == ".") root else byPath[node.path.substringBeforeLast('/', ".")] ?: root
            parent.add(byPath.getValue(node.path))
        }
        val tree = Tree(DefaultTreeModel(root))
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
            val node = (tree.lastSelectedPathComponent as? DefaultMutableTreeNode)?.userObject as? SceneNode
            if (node != null) controller.selected = node
        }
        tree.expandRow(0)
        controller.selected?.let { node -> byPath[node.path]?.let { tree.selectionPath = TreePath(it.path) } }
        JBScrollPane(tree)
    }
    Native(component, modifier)
}
