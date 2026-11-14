package app.anicanon.kumikae.as3.ui

import androidx.compose.runtime.Composable
import com.intellij.openapi.Disposable
import com.intellij.openapi.util.Disposer
import com.intellij.util.ui.UIUtil
import org.jetbrains.compose.swing.setContent
import java.awt.BorderLayout
import javax.swing.JPanel

/** The IDE owns the lifetime; Compose owns the tree. All mounting and disposal happens on the EDT. */
internal fun composePanel(owner: Disposable, content: @Composable () -> Unit): JPanel = JPanel(BorderLayout()).apply {
    val composition = setContent(content = content)
    Disposer.register(owner) {
        UIUtil.invokeLaterIfNeeded { composition.dispose() }
    }
}
