package app.anicanon.kumikae.as3.settings

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.panel.BackgroundWorker
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.ide.BrowserUtil
import com.intellij.openapi.fileChooser.FileChooser
import com.intellij.openapi.fileChooser.FileChooserDescriptor
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.DialogWrapper
import com.intellij.openapi.util.Disposer
import com.intellij.ui.components.ActionLink
import com.intellij.util.ui.JBUI
import org.jetbrains.compose.swing.components.ComboBox
import org.jetbrains.compose.swing.components.Label
import org.jetbrains.compose.swing.components.button.Button
import org.jetbrains.compose.swing.components.layout.Panel
import org.jetbrains.compose.swing.components.layout.PanelLayout
import org.jetbrains.compose.swing.node.SwingNode
import java.awt.FlowLayout
import java.nio.file.Path

internal class TesterDeliveryDialog(
    private val game: Project,
    private val spec: TesterBuild,
    private val access: TesterAccounts,
) : DialogWrapper(game) {
    private val worker = BackgroundWorker("Tester account").also { Disposer.register(disposable, it) }
    private val ios = spec.platform == "iOS"
    private var file by mutableStateOf(if (ios) "" else System.getenv("KUMIKAE_PLAY_OAUTH_CLIENT").orEmpty())
    private var issuer by mutableStateOf("")
    private var keyId by mutableStateOf("")
    private var connection by mutableStateOf<TesterConnection?>(null)
    private var target by mutableStateOf<TesterGroup?>(null)
    private var status by mutableStateOf("Loading saved account…")
    var approved: TesterConnection? = null
        private set

    init {
        title = if (ios) "Distribute to TestFlight" else "Distribute to Google Play"
        init()
        isOKActionEnabled = false
        setOKButtonText("Connect account")
        worker.submit(access::read) { result ->
            result.onSuccess { showConnection(it) }.onFailure { failed(it) }
        }
    }

    private fun showConnection(value: TesterConnection?) {
        connection = value
        target = value?.targets?.firstOrNull { it.id == value.selected } ?: value?.targets?.firstOrNull()
        status = ""
        isOKActionEnabled = true
        setOKButtonText(
            if (value !=
                null
            ) {
                "Distribute to testers"
            } else if (ios) {
                "Connect account"
            } else {
                "Sign in with Google"
            },
        )
        setErrorText(null)
    }

    private fun failed(error: Throwable) {
        status = ""
        isOKActionEnabled = true
        setErrorText(error.message ?: "Could not access the tester account.")
    }

    override fun createCenterPanel() = composePanel(disposable) {
        Column {
            SettingsSection("Build") {
                Label(spec.identifier)
                StatusLine("Version ${spec.version} · Build ${spec.build}" + if (ios) " · Team ${spec.team}" else "")
            }
            val saved = connection
            if (saved == null) {
                SettingsSection(if (ios) "App Store Connect account" else "Google Play account") {
                    StatusLine(
                        if (ios) {
                            "Choose a team API key from App Store Connect → Users and Access → Integrations."
                        } else {
                            "Sign in with Google using a desktop OAuth client for the Play Developer API."
                        },
                    )
                    LabelledField(if (ios) "Private key (.p8)" else "Desktop OAuth client (.json)", file, { file = it })
                    Panel(PanelLayout.Flow(FlowLayout.LEFT)) {
                        Button("Choose file…", {
                            FileChooser.chooseFile(
                                FileChooserDescriptor(true, false, false, false, false, false),
                                game,
                                null,
                            ) {
                                file = it.path
                                if (ios) keyId = it.nameWithoutExtension.removePrefix("AuthKey_")
                            }
                        })
                    }
                    Panel(PanelLayout.Flow(FlowLayout.LEFT)) {
                        SwingNode(factory = {
                            ActionLink(
                                if (ios) "Manage App Store Connect keys" else "Set up Google Play API access",
                            ).apply {
                                setExternalLinkIcon()
                                addActionListener {
                                    BrowserUtil.browse(
                                        if (ios) {
                                            "https://appstoreconnect.apple.com/access/integrations/api"
                                        } else {
                                            "https://developers.google.com/android-publisher/getting_started"
                                        },
                                    )
                                }
                            }
                        })
                    }
                    if (ios) {
                        LabelledField("Issuer ID", issuer, { issuer = it })
                        LabelledField("Key ID", keyId, { keyId = it })
                    }
                }
            } else {
                SettingsSection("Tester destination") {
                    Label(saved.label)
                    ComboBox(saved.targets, target, onSelectionChange = { target = it }) { Label(it.name) }
                    StatusLine(
                        if (ios) {
                            "A matching TestFlight build resumes here. Apple also includes groups with automatic distribution."
                        } else {
                            "This build will be released to testers on the internal track."
                        },
                    )
                    Panel(PanelLayout.Flow(FlowLayout.LEFT)) {
                        Button("Disconnect account", {
                            if (!worker.isBusy) {
                                isOKActionEnabled = false
                                worker.submit({ access.remember(null) }) { result ->
                                    result.onSuccess { showConnection(null) }.onFailure(::failed)
                                }
                            }
                        })
                    }
                }
            }
            if (status.isNotBlank()) StatusLine(status)
        }
    }.apply { preferredSize = JBUI.size(490, if (ios) 425 else 330) }

    override fun doOKAction() {
        if (worker.isBusy) return
        val saved = connection
        val selected = target
        setErrorText(null)
        isOKActionEnabled = false
        if (saved == null) {
            val account = TesterAccount(file.trim(), issuer.trim(), keyId.trim())
            status = if (ios) "Loading internal TestFlight groups…" else "Waiting for Google sign-in…"
            worker.submit({
                require(account.file.isNotBlank() && Path.of(account.file).isAbsolute) { "Choose a credential file." }
                access.connect(account).also(access::remember)
            }) { result -> result.onSuccess(::showConnection).onFailure(::failed) }
        } else if (selected != null) {
            saved.selected = selected.id
            worker.submit({ access.remember(saved) }) { result ->
                result.onSuccess {
                    approved = saved
                    close(OK_EXIT_CODE)
                }.onFailure(::failed)
            }
        } else {
            failed(IllegalStateException("Choose an internal tester group."))
        }
    }
}
