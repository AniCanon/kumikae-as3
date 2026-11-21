package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.panel.BackgroundWorker
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.Native
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.fileChooser.FileChooser
import com.intellij.openapi.fileChooser.FileChooserDescriptor
import com.intellij.openapi.fileChooser.FileChooserFactory
import com.intellij.openapi.fileChooser.FileSaverDescriptor
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.DialogWrapper
import com.intellij.openapi.util.Disposer
import com.intellij.ui.components.JBPasswordField
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import java.awt.FlowLayout
import java.nio.file.Path

internal class UploadKeyDialog(private val game: Project, preset: String, private val store: AndroidUploadKeys) :
    DialogWrapper(game) {
    private val worker = BackgroundWorker("Android signing key").also { Disposer.register(disposable, it) }
    private var path by mutableStateOf("")
    private var alias by mutableStateOf("upload")
    private var creating by mutableStateOf(false)
    private val password = JBPasswordField().apply { accessibleContext.accessibleName = "Keystore password" }
    private var busy by mutableStateOf(false)

    init {
        title = "Upload Key · $preset"
        setOKButtonText("Save key")
        init()
        if (store.configured) {
            busy = true
            isOKActionEnabled = false
            worker.submit(store::read) { result ->
                busy = false
                isOKActionEnabled = true
                result.onSuccess { key ->
                    if (key != null) {
                        path = key.path
                        alias = key.user
                    }
                }.onFailure { setErrorText("Could not read the saved key. Choose a keystore to replace it.") }
            }
        }
    }

    override fun createCenterPanel() = composePanel(disposable) {
        Column {
            SettingsSection("Release keystore") {
                LabelledField("Keystore file", path, {
                    path = it
                    creating = false
                })
                Panel(PanelLayout.Flow(FlowLayout.LEFT)) {
                    Button("Choose existing…", {
                        FileChooser.chooseFile(
                            FileChooserDescriptor(true, false, false, false, false, false),
                            game,
                            null,
                        ) {
                            creating = false
                            path = it.path
                        }
                    })
                    Button("Create new…", {
                        val chooser = FileChooserFactory.getInstance().createSaveFileDialog(
                            FileSaverDescriptor("Create Upload Keystore", "Choose a secure backup location.", "p12"),
                            game,
                        )
                        chooser.save(Path.of(System.getProperty("user.home")), "upload.p12")?.let {
                            path = it.file.absolutePath
                            creating = true
                        }
                    })
                }
                LabelledField("Key alias", alias, { alias = it })
                Label("Keystore password")
                Native(password)
                StatusLine(
                    if (creating) {
                        "Back up the new keystore and password. Keep them outside your game files."
                    } else {
                        "Enter the password shared by the keystore and its private key."
                    },
                )
            }
            StatusLine(
                if (busy) {
                    "Checking signing details…"
                } else {
                    "Rider stores these signing details in its password store for this project and preset."
                },
            )
            if (store.configured) {
                Button("Use Godot signing settings", {
                    if (!busy) save(null, false)
                })
            }
        }
    }.apply { minimumSize = JBUI.size(420, 280) }

    override fun doCancelAction() = super.doCancelAction()

    override fun doOKAction() {
        if (busy) return
        val chars = password.password
        val key = try {
            UploadKey(path.trim(), alias.trim(), String(chars))
        } finally {
            chars.fill('\u0000')
        }
        save(key, creating)
    }

    private fun save(key: UploadKey?, create: Boolean) {
        busy = true
        isOKActionEnabled = false
        worker.submit({
            if (key != null) {
                if (create) key.create()
                key.validate()
            }
            store.save(key)
        }) { result ->
            busy = false
            isOKActionEnabled = true
            result.onSuccess {
                password.text = ""
                close(OK_EXIT_CODE)
            }
                .onFailure { setErrorText(it.message ?: "Could not save the upload key.") }
        }
    }
}
