import org.jetbrains.intellij.platform.gradle.tasks.PrepareSandboxTask
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
            // The alpha Compose Swing library is only known to work in this exact build.
            sinceBuild = "262.10968.170"
            untilBuild = "262.10968.170"
        }
    }
    pluginVerification {
        ides { local(riderHome.get()) }
    }
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
        if (name == "prepareSandbox_runIde") from(uiProbeJar) { into("kumikae-as3-ui-probe/lib") }
    }
    tasks.runIde {
        val report = layout.buildDirectory.dir("reports/ui").get().asFile
        args("kumikae-as3-ui-check", report.absolutePath)
        timeout = Duration.ofMinutes(2)
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
    from(root.resolve("compiler/build")) {
        include("kas3c")
        into("${intellijPlatform.projectName.get()}/native")
    }
    from(root.resolve("compiler/tests/godot_project/bin")) {
        include("libkas3_godot.*")
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
tasks.runIde {
    systemProperty("idea.log.debug.categories", "#com.intellij.platform.lsp")
    providers.gradleProperty("lspTrace").orNull?.let { environment("KUMIKAE_AS3_LSP_TRACE", it) }
}
