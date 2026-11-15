package app.anicanon.kumikae.as3.simulator

import com.google.gson.Gson
import org.junit.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class ProbeDeviceOwnerTest {
    @Test fun lostCreateReplyStillDeletesOnlyTheNewProbe() {
        val runner = Runner().apply { loseCreateReply = true }
        assertFailsWith<IllegalStateException> { ProbeDeviceOwner(runner).use("type", "runtime") {} }
        assertTrue(runner.deleted, "A device was created even though the command failed")
        assertEquals(listOf("00000000-0000-0000-0000-000000000001"), runner.modified)
    }

    @Test fun failedWorkAndFailedStateReadStillAttemptDeletion() {
        val runner = Runner()
        assertFailsWith<IllegalStateException> {
            ProbeDeviceOwner(runner).use("type", "runtime") {
                runner.failList = true
                error("probe failed")
            }
        }
        assertTrue(runner.deleted)
    }

    @Test fun anExistingProbeIsNeverAdopted() {
        val runner = Runner().apply { exists = true }
        assertFailsWith<IllegalStateException> { ProbeDeviceOwner(runner).use("type", "runtime") {} }
        assertEquals(emptyList(), runner.modified)
    }

    private class Runner : CommandRunner {
        var exists = false
        var deleted = false
        var loseCreateReply = false
        var failList = false
        val modified = mutableListOf<String>()
        private val id = "00000000-0000-0000-0000-000000000001"

        override fun run(command: List<String>, timeoutMs: Long): String = when (command[2]) {
            "list" -> {
                check(!failList) { "state read failed" }
                val found = if (exists) {
                    listOf(mapOf("udid" to id, "name" to "kas3-probe", "isAvailable" to true, "state" to "Shutdown"))
                } else {
                    emptyList()
                }
                Gson().toJson(mapOf("devices" to mapOf("com.apple.CoreSimulator.SimRuntime.iOS-test" to found)))
            }

            "create" -> {
                exists = true
                check(!loseCreateReply) { "create reply lost" }
                id
            }

            "delete" -> {
                modified += command[3]
                exists = false
                deleted = true
                ""
            }

            else -> error("Unexpected probe command")
        }
    }
}
