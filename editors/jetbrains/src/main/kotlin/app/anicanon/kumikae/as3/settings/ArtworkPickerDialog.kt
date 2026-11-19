package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Native
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.fileChooser.FileChooser
import com.intellij.openapi.fileChooser.FileChooserDescriptor
import com.intellij.openapi.progress.ProgressManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.DialogWrapper
import com.intellij.openapi.util.ThrowableComputable
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.ui.DocumentAdapter
import com.intellij.ui.SearchTextField
import com.intellij.ui.components.JBScrollPane
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.modifier.SwingModifier
import java.awt.event.KeyAdapter
import java.awt.event.KeyEvent
import javax.swing.JComponent
import javax.swing.event.DocumentEvent

internal class ArtworkPickerDialog(
    private val project: Project,
    private val root: VirtualFile,
    private val kind: GameResourceKind,
    private val current: String,
    private val layout: SplashLayout?,
) : DialogWrapper(project) {
    private val library = ProjectArtworkLibrary(root, disposable)
    private val gallery = ArtworkGallery()
    private val scroll = JBScrollPane(gallery)
    private val search = SearchTextField(false)
    private var choices: ArtworkChoices? = null
    private var selection by mutableStateOf<String?>(null)
    private var preview by mutableStateOf<Artwork?>(null)
    private var loading by mutableStateOf(false)
    private var selectable by mutableStateOf(false)
    private var requested = emptyList<String>()
    private var browsedPath: String? = null
    private var imported: String? = null
    val result: String? get() = imported ?: selection

    init {
        title = "Choose " + kind.title.lowercase()
        setOKButtonText(if (kind == GameResourceKind.Splash) "Use Splash" else "Use Icon")
        isOKActionEnabled = false
        search.textEditor.emptyText.text = "Search project images"
        search.textEditor.accessibleContext.accessibleName = "Search project images"
        search.addDocumentListener(object : DocumentAdapter() {
            override fun textChanged(e: DocumentEvent) = filter()
        })
        search.textEditor.addKeyListener(object : KeyAdapter() {
            override fun keyPressed(e: KeyEvent) {
                if (e.keyCode == KeyEvent.VK_DOWN && !gallery.isSelectionEmpty) {
                    gallery.requestFocusInWindow()
                    e.consume()
                }
            }
        })
        gallery.emptyText.text = "Finding project images…"
        gallery.addListSelectionListener { if (!it.valueIsAdjusting) select(gallery.selectedValue) }
        scroll.viewport.addChangeListener { loadVisible() }
        init()
        library.list(kind, current) {
            choices = it
            filter(browsedPath ?: selection)
        }
    }

    private fun filter(prefer: String? = selection) {
        val matches = choices?.search(search.text) ?: return
        val extra = browsedPath?.takeIf { it !in matches && it.contains(search.text.trim(), ignoreCase = true) }
        val paths = listOfNotNull(extra) + matches
        val chosen = prefer?.takeIf(paths::contains) ?: current.takeIf(paths::contains) ?: paths.firstOrNull()
        gallery.emptyText.text = when {
            search.text.isNotBlank() -> "No images match your search"
            kind == GameResourceKind.Icon -> "No project images found"
            else -> "No PNG images in this project"
        }
        gallery.setListData(paths.toTypedArray())
        if (chosen != null) gallery.setSelectedValue(chosen, true)
        loadVisible()
    }

    private fun select(path: String?) {
        setErrorText(null)
        setOKButtonText(
            when {
                path != null && path == library.external?.url -> "Import and Use"
                kind == GameResourceKind.Splash -> "Use Splash"
                else -> "Use Icon"
            },
        )
        selection = path
        preview = null
        loading = path != null
        selectable = false
        isOKActionEnabled = false
        if (path == null) return
        library.preview(path, layout?.filter != false) { image ->
            if (path == selection) {
                preview = image.art
                loading = false
                selectable = image.selectable
                isOKActionEnabled = image.selectable
            }
        }
    }

    private fun loadVisible() {
        val missing = gallery.missingVisible()
        if (missing.isEmpty() || missing == requested) return
        requested = missing
        library.thumbnails(missing) {
            requested = emptyList()
            if (it.isNotEmpty()) {
                gallery.showThumbnails(it)
                loadVisible()
            }
        }
    }

    private fun browse() {
        val descriptor = FileChooserDescriptor(true, false, false, false, false, false)
            .withTitle("Choose image to import").withFileFilter { it.isInLocalFileSystem && kind.accepts(it.name) }
        FileChooser.chooseFile(descriptor, project, root) { file ->
            stage(file)
        }
    }

    internal fun stage(file: VirtualFile) {
        if (!kind.accepts(file.name)) return
        val path = library.stage(file)
        browsedPath = path
        search.text = ""
        filter(path)
    }

    override fun doOKAction() {
        val path = selection ?: return
        if (!isOKActionEnabled) return
        if (path == library.external?.url) {
            val task = ThrowableComputable<String, Exception> { library.importFile(path) }
            try {
                imported = ProgressManager.getInstance()
                    .runProcessWithProgressSynchronously(task, "Importing artwork", false, project)
            } catch (failure: Exception) {
                setErrorText(failure.message ?: "Could not import the image.")
                return
            }
        }
        super.doOKAction()
    }

    override fun createCenterPanel(): JComponent = composePanel(disposable) {
        Panel(PanelLayout.Border(0, JBUI.scale(12))) {
            Panel(PanelLayout.Border(JBUI.scale(8), 0), modifier = SwingModifier.north()) {
                Native(search, SwingModifier.center())
                Button("Browse…", ::browse, SwingModifier.east())
            }
            Native(scroll, SwingModifier.center())
            ArtworkPickerDetail(selection, current, preview, loading, layout, selectable, SwingModifier.east())
        }
    }.apply { preferredSize = JBUI.size(740, 405) }

    override fun getPreferredFocusedComponent(): JComponent = search.textEditor
    override fun getDimensionServiceKey() = "Kumikae.ArtworkPicker"
}
