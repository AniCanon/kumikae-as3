plugins {
    kotlin("jvm") version "2.4.10"
    id("org.jetbrains.intellij.platform") version "2.20.0"
}

group = "org.kumikae.as3"
version = "0.1.0"

repositories {
    mavenCentral()
    intellijPlatform { defaultRepositories() }
}

dependencies {
    implementation(project(":core")) {
        // The IDE supplies Kotlin at runtime; the standalone core distribution carries its own copy.
        exclude(group = "org.jetbrains.kotlin", module = "kotlin-stdlib")
    }
    intellijPlatform {
        local(providers.gradleProperty("idePath").getOrElse("/Applications/Rider.app"))
    }
}

kotlin { jvmToolchain(21) }
intellijPlatform {
    buildSearchableOptions = false
    instrumentCode = false
    pluginConfiguration {
        name = "Kumikae AS3"
        ideaVersion { sinceBuild = "262" }
    }
    sandboxContainer = layout.buildDirectory.dir("idea-sandbox")
}
