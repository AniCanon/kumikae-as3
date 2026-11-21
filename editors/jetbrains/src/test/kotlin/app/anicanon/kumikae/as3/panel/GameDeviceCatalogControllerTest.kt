package app.anicanon.kumikae.as3.panel

import org.junit.jupiter.api.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class GameDeviceCatalogControllerTest {
    @Test fun `refresh populates simulators and emulators and disposal drops late deliveries`() {
        val iosTasks = ManualBackgroundTasks()
        val androidTasks = ManualBackgroundTasks()
        val companionTasks = ManualBackgroundTasks()
        val iosRepo = FakeSimulatorRepository().apply {
            listed = listOf(SimulatorDevice("sim-1", "iPhone 17", "iOS 19", BOOTED))
        }
        var savedGraphics = AndroidGraphics.entries.first()
        val controller = GameDeviceCatalogController(
            ios = iosRepo,
            listAndroid = { listOf(AndroidAvd("Pixel_9", "emulator-5554")) },
            initialGraphics = savedGraphics,
            persistGraphics = { savedGraphics = it },
            readCompanions = { emptyList() },
            hasCompanions = { false },
            iosWorker = iosTasks,
            androidWorker = androidTasks,
            companionWorker = companionTasks,
        )
        controller.onVisibilityChanged(true)
        assertTrue(controller.refreshing)
        iosTasks.finish()
        androidTasks.finish()
        assertFalse(controller.refreshing)
        assertEquals(1, controller.simulators.size)
        assertEquals(1, controller.emulators.size)
        val nextGraphics = AndroidGraphics.entries.last()
        controller.selectGraphics(nextGraphics)
        assertEquals(nextGraphics, savedGraphics)

        controller.refresh()
        iosTasks.runWork()
        controller.dispose()
        iosTasks.deliver()
        assertTrue(iosTasks.disposed)
        assertTrue(androidTasks.disposed)
        assertTrue(companionTasks.disposed)
    }

    @Test fun `companion plugin change discards stale companion results`() {
        val iosTasks = ManualBackgroundTasks()
        val androidTasks = ManualBackgroundTasks()
        val firstCompanion = ManualBackgroundTasks()
        val secondCompanion = ManualBackgroundTasks()
        var currentGroups = listOf(DeviceGroup("Old", emptyList(), "Disconnected"))
        val controller = GameDeviceCatalogController(
            ios = null,
            listAndroid = { emptyList() },
            initialGraphics = AndroidGraphics.entries.first(),
            persistGraphics = {},
            readCompanions = { currentGroups },
            hasCompanions = { true },
            iosWorker = iosTasks,
            androidWorker = androidTasks,
            companionWorker = firstCompanion,
            newCompanionWorker = { secondCompanion },
        )
        controller.onVisibilityChanged(true)
        androidTasks.finish()
        firstCompanion.runWork()

        currentGroups = listOf(DeviceGroup("New", emptyList(), "Ready"))
        controller.onCompanionsChanged()
        firstCompanion.deliver()
        assertEquals(emptyList(), controller.groups)

        secondCompanion.finish()
        assertEquals(listOf("New"), controller.groups.map { it.label })
    }
}
