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

    /** For a user that must not close what others share. */
    val repository: SimulatorRepository = SharedRepository(ApiSimulatorRepository(service))

    override fun dispose() = service.close()
}

/**
 * A repository that several users hold: its `close` does nothing, and the one who made it closes the
 * service. Delegation alone is not enough here: a call the interface gives a default for (live
 * pictures, which a repository may not have) would get the default, not the repository behind this one.
 */
internal class SharedRepository(private val inner: SimulatorRepository) : SimulatorRepository by inner {
    override fun openFrames(device: String): SimulatorFrames = inner.openFrames(device)

    override fun close() = Unit
}
