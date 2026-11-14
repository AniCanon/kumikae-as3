package app.anicanon.kumikae.as3.panel

import java.nio.file.Path

/** Composition root: concrete services are chosen here, not by the view or the controller. */
internal object SimulatorComposition {
    fun controller(
        stateDirectory: Path,
        inputAllowed: () -> Boolean,
        rememberInput: (Boolean) -> Unit,
    ): SimulatorWindowController = SimulatorWindowController(
        ApiSimulatorRepository(LocalSimulatorService(stateDirectory)),
        actions = BackgroundWorker("Kumikae AS3 Simulator actions"),
        polling = BackgroundWorker("Kumikae AS3 Simulator polling"),
        inputAllowed = inputAllowed,
        rememberInput = rememberInput,
    )
}
