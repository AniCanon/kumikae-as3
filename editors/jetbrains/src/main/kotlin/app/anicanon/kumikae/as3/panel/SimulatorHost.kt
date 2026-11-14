package app.anicanon.kumikae.as3.panel

import com.intellij.openapi.Disposable
import com.intellij.openapi.application.PathManager
import com.intellij.openapi.components.Service
import java.nio.file.Path

/**
 * The project's one Simulator service: the panel and a run configuration share its selection and its
 * console. It is started by whoever asks first and closed with the project.
 */
@Service(Service.Level.PROJECT)
internal class SimulatorHost : Disposable {
    private val service = LocalSimulatorService(Path.of(PathManager.getSystemPath(), "kumikae-as3"))

    /** For a user that must not close what others share: its `close` does nothing. */
    val repository: SimulatorRepository = object : SimulatorRepository by ApiSimulatorRepository(service) {
        override fun close() = Unit
    }

    override fun dispose() = service.close()
}
