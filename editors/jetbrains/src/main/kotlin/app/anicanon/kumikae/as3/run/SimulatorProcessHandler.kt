package app.anicanon.kumikae.as3.run

import app.anicanon.kumikae.as3.panel.SimulatorRepository
import com.intellij.execution.process.ProcessHandler
import com.intellij.execution.process.ProcessOutputTypes
import com.intellij.openapi.diagnostic.Logger
import com.intellij.util.concurrency.AppExecutorUtil
import java.io.OutputStream
import java.util.concurrent.atomic.AtomicBoolean

/** What the IDE's Run window holds for an app on the Simulator: it is not a process of this Mac's. */
internal class SimulatorProcessHandler(
    repository: SimulatorRepository,
    private val bundle: String,
    appPath: String,
    build: (((String, Boolean) -> Unit) -> String)? = null,
    sources: String = "",
    destination: String? = null,
) : ProcessHandler() {
    private val stopping = AtomicBoolean()
    private val run = SimulatorRun(
        repository,
        bundle,
        appPath,
        { text, error ->
            notifyTextAvailable(text, if (error) ProcessOutputTypes.STDERR else ProcessOutputTypes.STDOUT)
        },
        sources,
        build = build,
        destination = destination,
    )
    private val worker = Thread(::follow, "Kumikae AS3 Simulator run").apply { isDaemon = true }

    override fun startNotify() {
        super.startNotify()
        worker.start()
    }

    private fun follow() {
        val code = try {
            notifyTextAvailable("Launching $bundle on the Simulator…\n", ProcessOutputTypes.SYSTEM)
            run.start()
            var ended: Int? = null
            while (ended == null && !stopping.get()) {
                ended = run.pump()
                if (ended == null) Thread.sleep(POLL_MILLIS)
            }
            ended ?: 0
        } catch (interrupted: InterruptedException) {
            Thread.currentThread().interrupt()
            1
        } catch (failure: Exception) {
            notifyTextAvailable("${failure.message ?: failure.javaClass.simpleName}\n", ProcessOutputTypes.STDERR)
            1
        }
        if (!isProcessTerminated) notifyProcessTerminated(code)
    }

    override fun destroyProcessImpl() {
        stopping.set(true)
        worker.interrupt()
        AppExecutorUtil.getAppExecutorService().execute {
            runCatching(run::stop).onFailure {
                Logger.getInstance(SimulatorProcessHandler::class.java).warn("Stopping Simulator app $bundle", it)
            }
        }
    }

    override fun detachProcessImpl() {
        // Detached, the app keeps running on the Simulator; only the Run window stops following it.
        stopping.set(true)
        worker.interrupt()
        notifyProcessDetached()
    }

    override fun detachIsDefault() = false

    override fun getProcessInput(): OutputStream? = null

    private companion object {
        const val POLL_MILLIS = 300L
    }
}
