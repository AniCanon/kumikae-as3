package app.anicanon.kumikae.as3.panel

/** Composition root: concrete services are chosen here, not by the view or the controller. */
internal object SimulatorComposition {
    fun controller(
        repository: SimulatorRepository,
        inputAllowed: () -> Boolean,
        rememberInput: (Boolean) -> Unit,
    ): SimulatorWindowController = SimulatorWindowController(
        repository,
        actions = BackgroundWorker("Kumikae AS3 Simulator actions"),
        polling = BackgroundWorker("Kumikae AS3 Simulator polling"),
        streams = BackgroundWorker("Kumikae AS3 Simulator stream startup"),
        touches = BackgroundWorker("Kumikae AS3 Simulator touch"),
        inputAllowed = inputAllowed,
        rememberInput = rememberInput,
    )
}
