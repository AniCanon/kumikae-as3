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
}

tasks.test { useJUnitPlatform() }
