plugins {
    kotlin("jvm")
    application
}

repositories { mavenCentral() }

dependencies {
    implementation(kotlin("stdlib"))
    implementation("com.google.code.gson:gson:2.13.2")
    testImplementation(kotlin("test"))
    testImplementation("junit:junit:4.13.2")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
    testRuntimeOnly("org.junit.vintage:junit-vintage-engine:5.12.2")
}

kotlin { jvmToolchain(25) }
application {
    mainClass = "app.anicanon.kumikae.as3.simulator.MainKt"
    applicationName = "kumikae-simulator"
}

tasks.test { useJUnitPlatform() }
