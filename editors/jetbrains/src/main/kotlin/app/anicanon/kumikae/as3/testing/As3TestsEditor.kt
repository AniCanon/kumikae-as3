package app.anicanon.kumikae.as3.testing

import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.SettingsSection
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.composePanel
import com.google.gson.JsonParser
import com.intellij.openapi.application.ModalityState
import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.options.ConfigurationException
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.progress.ProcessCanceledException
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.util.concurrency.AppExecutorUtil
import org.jetbrains.compose.swing.components.button.CheckBox
import java.nio.file.Path
import javax.swing.JComponent

internal class As3TestsEditor : SettingsEditor<As3TestsConfiguration>() {
    private var groups by mutableStateOf(emptyList<String>())
    private var selection by mutableStateOf(emptySet<String>())
    private var all by mutableStateOf(true)
    private var native by mutableStateOf(false)
    private var device by mutableStateOf("")
    private var notice by mutableStateOf("")
    override fun resetEditorFrom(configuration: As3TestsConfiguration) {
        val initialSelection = configuration.options.cases.lineSequence().filter(String::isNotBlank).toSet()
        selection = initialSelection
        all = initialSelection.isEmpty()
        native = configuration.options.native
        device = configuration.options.device
        groups = initialSelection.toList()
        notice = "Loading tests…"
        val basePath = configuration.project.basePath
        val manifest = configuration.options.manifest
        ReadAction.nonBlocking<Result<List<String>>> {
            try {
                val path = Path.of(requireNotNull(basePath)).resolve(manifest)
                val file = requireNotNull(LocalFileSystem.getInstance().findFileByNioFile(path)) {
                    "Open a game with a test target."
                }
                val text = requireNotNull(FileDocumentManager.getInstance().getDocument(file)).text
                val found = JsonParser.parseString(text).asJsonObject.getAsJsonArray("tests")
                    .map { it.asJsonObject["name"].asString }
                Result.success(found)
            } catch (canceled: ProcessCanceledException) {
                throw canceled
            } catch (error: Exception) {
                Result.failure(error)
            }
        }.coalesceBy(this).expireWith(this).finishOnUiThread(ModalityState.any()) { result ->
            val found = result.onSuccess { notice = "" }
                .onFailure { notice = it.message ?: "Cannot read the game's tests." }
                .getOrDefault(emptyList())
            groups = (found + selection).distinct()
        }.submit(AppExecutorUtil.getAppExecutorService())
    }
    override fun applyEditorTo(configuration: As3TestsConfiguration) {
        if (!all && selection.isEmpty()) throw ConfigurationException("Choose at least one test for this plan.")
        configuration.options.cases = if (all) "" else selection.joinToString("\n")
        configuration.options.native = native
        configuration.options.device = device.trim()
    }
    override fun createEditor(): JComponent = composePanel(this) {
        SettingsSection("Test selection") {
            CheckBox("All game tests", all, { checked: Boolean ->
                all = checked
                fireEditorStateChanged()
            })
            CheckBox("Run Godot tests with native code", native, { checked: Boolean ->
                native = checked
                fireEditorStateChanged()
            })
            LabelledField(
                label = "Mobile device",
                value = device,
                placeholder = "Auto-detect booted simulator or emulator",
                onChange = { updated ->
                    device = updated
                    fireEditorStateChanged()
                },
            )
            for (group in groups) {
                key(group) {
                    CheckBox(group.replace("::", " › "), all || group in selection, { checked: Boolean ->
                        if (all) selection = groups.toSet()
                        all = false
                        selection = if (checked) selection + group else selection - group
                        fireEditorStateChanged()
                    })
                }
            }
            if (notice.isNotEmpty()) StatusLine(notice)
        }
    }
}
