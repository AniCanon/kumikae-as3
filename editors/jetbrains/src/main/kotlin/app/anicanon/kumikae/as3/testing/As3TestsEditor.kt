package app.anicanon.kumikae.as3.testing

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import app.anicanon.kumikae.as3.ui.Column
import app.anicanon.kumikae.as3.ui.LabelledField
import app.anicanon.kumikae.as3.ui.StatusLine
import app.anicanon.kumikae.as3.ui.composePanel
import com.intellij.openapi.options.SettingsEditor
import javax.swing.JComponent

internal class As3TestsEditor : SettingsEditor<As3TestsConfiguration>() {
    private var manifest by mutableStateOf("kas3-tests.json")
    private var cases by mutableStateOf("")
    override fun resetEditorFrom(configuration: As3TestsConfiguration) {
        manifest = configuration.options.manifest
        cases = configuration.options.cases
    }
    override fun applyEditorTo(configuration: As3TestsConfiguration) {
        configuration.options.manifest = manifest.trim()
        configuration.options.cases = cases.trim()
    }
    override fun createEditor(): JComponent = composePanel(this) {
        Column {
            LabelledField("Test manifest", manifest, {
                manifest = it
                fireEditorStateChanged()
            })
            LabelledField("Registered test name (empty: all tests)", cases, {
                cases = it
                fireEditorStateChanged()
            })
            StatusLine("Results, failure details and source navigation appear in the Tests window.")
        }
    }
}
