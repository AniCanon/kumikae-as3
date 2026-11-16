import org.jetbrains.intellij.platform.gradle.tasks.PrepareSandboxTask
import org.jetbrains.intellij.platform.gradle.tasks.RunIdeTask
import java.time.Duration

plugins {
    kotlin("jvm") version "2.4.10"
    kotlin("plugin.compose") version "2.4.10"
    id("org.jetbrains.intellij.platform") version "2.20.0"
    id("org.jlleitschuh.gradle.ktlint") version "14.2.0"
}

group = "app.anicanon.kumikae.as3"
version = "0.1.0"

repositories {
    mavenCentral()
    google()
    maven("https://packages.jetbrains.team/maven/p/cmp/dev") {
        content { includeGroup("org.jetbrains.compose.swing") }
    }
    intellijPlatform { defaultRepositories() }
}

val riderHome = providers.environmentVariable("RIDER_HOME").orElse("/Applications/Rider.app")
val lsp4ijVersion = "0.21.0"
val ideaHome = providers.environmentVariable("IDEA_HOME").orElse("/Applications/IntelliJ IDEA.app")
dependencies {
    implementation(project(":core")) {
        // The IDE supplies Kotlin at runtime; the standalone core distribution carries its own copy.
        exclude(group = "org.jetbrains.kotlin", module = "kotlin-stdlib")
    }
    implementation("org.jetbrains.compose.swing:swing-ui:0.1.0-alpha06")
    intellijPlatform {
        local(riderHome.get())
        bundledPlugin("org.jetbrains.plugins.textmate")
        bundledModule("intellij.platform.lsp")
        bundledPlugin("intellij.testRunner.plugin")
        plugin("com.redhat.devtools.lsp4ij", lsp4ijVersion)
        pluginVerifier()
    }
    testImplementation(kotlin("test"))
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.junit.jupiter:junit-jupiter:5.12.2")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
    testRuntimeOnly("org.junit.vintage:junit-vintage-engine:5.12.2")
}

allprojects {
    apply(plugin = "org.jlleitschuh.gradle.ktlint")
    extensions.configure<org.jlleitschuh.gradle.ktlint.KtlintExtension> { version.set("1.8.0") }
}

kotlin { jvmToolchain(25) }
// One install script: the one a person runs from tools/ is the one the plugin carries.
sourceSets.main { resources.srcDir("tools") }
tasks.buildPlugin { dependsOn(tasks.ktlintCheck, project(":core").tasks.named("ktlintCheck")) }
intellijPlatform {
    buildSearchableOptions = false
    pluginConfiguration {
        name = "Kumikae AS3"
        ideaVersion {
            // Runtime probes and verification cover the installed IDEA and Rider endpoints.
            sinceBuild = "261.22158.277"
            untilBuild = "262.10968.170"
        }
    }
    pluginVerification {
        ides {
            local(riderHome.get())
            local(ideaHome.get())
        }
    }
}

intellijPlatformTesting.runIde.register("runIdea") {
    localPath = ideaHome.map { file(it) }
    plugins {
        plugin("com.redhat.devtools.lsp4ij", lsp4ijVersion)
    }
}

intellijPlatformTesting.runIde.register("runIdeaWithoutLsp4ij") {
    localPath = ideaHome.map { file(it) }
    plugins { disablePlugin("com.redhat.devtools.lsp4ij") }
    task { environment("KUMIKAE_MISSING_CLIENT_PROBE", "1") }
}

intellijPlatformTesting.runIde.register("runRiderWithoutLsp4ij") {
    localPath = riderHome.map { file(it) }
    plugins { disablePlugin("com.redhat.devtools.lsp4ij") }
}

tasks.test { useJUnitPlatform() }

// Opt-in UI probe is a second plugin, never part of the distributable plugin ZIP.
val uiProbeJar = tasks.register<Jar>("uiProbeJar") {
    dependsOn(tasks.testClasses)
    archiveBaseName = "kumikae-as3-ui-probe"
    from(sourceSets.test.get().output) { include("**/UiProbe*.class") }
    from("src/uiProbe/plugin.xml") { into("META-INF") }
}
if (providers.gradleProperty("uiProbe").isPresent) {
    tasks.withType<PrepareSandboxTask>().configureEach {
        if (name.startsWith("prepareSandbox_run")) {
            from(uiProbeJar) { into("kumikae-as3-ui-probe/lib") }
        }
    }
    tasks.withType<RunIdeTask>().configureEach {
        val report = layout.buildDirectory.dir(
            when (name) {
                "runIde" -> "reports/ui"
                "runIdea" -> "reports/ui-idea"
                else -> "reports/ui-$name"
            },
        ).get().asFile
        args("kumikae-as3-ui-check", report.absolutePath)
        if (listOf("KUMIKAE_COMPLETION_RETENTION_CHECK", "KUMIKAE_COMPLETION_RETENTION_PROBE")
                .any { providers.environmentVariable(it).orNull == "1" }
        ) {
            // Leak checks must collect soft caches too; this flag applies only to the probe JVM.
            jvmArgs("-XX:SoftRefLRUPolicyMSPerMB=0")
        }
        timeout =
            Duration.ofMinutes(
                if (providers.environmentVariable("KUMIKAE_COMPLETION_RETENTION_PROBE").orNull ==
                    "1"
                ) {
                    15
                } else {
                    2
                },
            )
        doFirst { report.resolve("result.txt").delete() }
        doLast {
            check(report.resolve("result.txt").takeIf { it.isFile }?.readText()?.trim() == "passed") {
                "UI probe did not pass. See build/reports/ui/result.txt and the sandbox log."
            }
        }
    }
}

// The compiler and the Godot extension, from a build of this repository's compiler (or of the checkout
// named with -Pkas3Root=...), so that a new project runs without anything else being set up. Without a
// build the plugin is made without them and looks for them on the computer.
val kas3Root = providers.gradleProperty("kas3Root").orElse(rootDir.resolve("../..").path)
tasks.withType<PrepareSandboxTask>().configureEach {
    val root = file(kas3Root.get())
    from(rootDir.resolve("../../testing")) {
        include("*.py", "as3/**", "README.md")
        into("${intellijPlatform.projectName.get()}/native/testing")
    }
    from(root.resolve("compiler/build")) {
        include("kas3c", "kas3c.exe")
        into("${intellijPlatform.projectName.get()}/native")
    }
    from(root.resolve("compiler/tests/godot_project/bin")) {
        include("libkas3_godot.*")
        into("${intellijPlatform.projectName.get()}/native")
    }
    // The extension for iOS, when it has been built (compiler/tests/build_ios.sh).
    from(root.resolve("compiler/tests/godot_project/bin/ios")) {
        into("${intellijPlatform.projectName.get()}/native/ios")
    }
}

// The formatter ships with Xcode. Never download a separate lint dependency.
val nativeSwiftAvailable = providers.provider {
    System.getProperty("os.name").contains("Mac") &&
        file("/usr/bin/xcrun").canExecute() && providers.exec {
            commandLine("/usr/bin/xcrun", "--find", "swift")
            isIgnoreExitValue = true
        }.result.get().exitValue == 0
}
val swiftFiles = listOf("Package.swift", "Sources", "Tests", "ProbeApp")
val lintSimulatorBridge = tasks.register<Exec>("lintSimulatorBridge") {
    workingDir("native/SimulatorBridge")
    commandLine(listOf("/usr/bin/xcrun", "swift-format", "lint", "--strict", "--recursive") + swiftFiles)
    onlyIf("installed Swift on macOS is required") { nativeSwiftAvailable.get() }
}
tasks.register<Exec>("formatSimulatorBridge") {
    workingDir("native/SimulatorBridge")
    commandLine(listOf("/usr/bin/xcrun", "swift-format", "format", "--in-place", "--recursive") + swiftFiles)
    onlyIf("installed Swift on macOS is required") { nativeSwiftAvailable.get() }
}
tasks.check { dependsOn(lintSimulatorBridge) }
tasks.buildPlugin {
    dependsOn(lintSimulatorBridge)
    // The zip records no execute bit of its own; the programs it carries need theirs to run.
    filesMatching(listOf("**/native/kas3c", "**/native/simulator-bridge")) { permissions { unix("rwxr-xr-x") } }
}

// Native Simulator helper: SwiftPM's resource bundle must travel beside its executable.
val buildSimulatorBridge = tasks.register<Exec>("buildSimulatorBridge") {
    workingDir("native/SimulatorBridge")
    commandLine("/usr/bin/xcrun", "swift", "build", "-c", "release")
    dependsOn(lintSimulatorBridge)
    onlyIf("installed Swift on macOS is required") { nativeSwiftAvailable.get() }
}
tasks.runIde {
    if (providers.environmentVariable("KUMIKAE_SIMULATOR_ENDURANCE_PROBE").orNull == "1" ||
        providers.environmentVariable("KUMIKAE_SIMULATOR_MEMORY_PROBE").orNull == "1"
    ) {
        timeout = Duration.ofMinutes(12)
    }
}
tasks.withType<PrepareSandboxTask>().configureEach {
    dependsOn(buildSimulatorBridge)
    from("native/SimulatorBridge/.build/release") {
        include("simulator-bridge", "SimulatorBridge_SimulatorBridge.bundle/**")
        into("${intellijPlatform.projectName.get()}/native")
    }
}

// One grammar: the VS Code client's, copied into the plugin's folder for the IDE's TextMate support.
tasks.withType<PrepareSandboxTask>().configureEach {
    from("../vscode") {
        include("package.json", "language-configuration.json", "syntaxes/**")
        into("${intellijPlatform.projectName.get()}/textmate/actionscript")
    }
}

// What the IDE and the language server say to each other goes to the sandbox's log: when a feature
// does not work in the editor, the log shows whether it was asked for and what came back.
tasks.withType<RunIdeTask>().configureEach {
    systemProperty("idea.log.debug.categories", "#com.intellij.platform.lsp")
    providers.gradleProperty("lspTrace").orNull?.let { environment("KUMIKAE_AS3_LSP_TRACE", it) }
}
