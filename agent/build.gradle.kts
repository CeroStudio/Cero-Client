import java.awt.GraphicsEnvironment
import javax.swing.JOptionPane

plugins {
    id("java")
    id("com.gradleup.shadow") version "8.3.5"
}

group = "fr.cerostudio"
version = "1.0-SNAPSHOT"

java {
    sourceCompatibility = JavaVersion.VERSION_1_8
    targetCompatibility = JavaVersion.VERSION_1_8
}

repositories {
    mavenCentral()
    maven("https://repo.spongepowered.org/maven/")
}

dependencies {
    implementation("org.spongepowered:mixin:0.8.5")
    implementation("net.fabricmc:tiny-remapper:0.10.4")

    implementation("org.ow2.asm:asm:9.7")
    implementation("org.ow2.asm:asm-commons:9.7")
    implementation("org.ow2.asm:asm-tree:9.7")
    implementation("org.ow2.asm:asm-util:9.7")

    implementation("com.google.guava:guava:32.1.3-jre")
    implementation("com.google.code.gson:gson:2.10.1")

    testImplementation(platform("org.junit:junit-bom:5.10.0"))
    testImplementation("org.junit.jupiter:junit-jupiter")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
}

tasks.test {
    useJUnitPlatform()
}

tasks.shadowJar {
    archiveBaseName.set("CeroClient-MC")
    archiveVersion.set("")
    archiveClassifier.set("")

    manifest {
        attributes(
            "Main-Class" to "fr.cerostudio.Main",
            "MixinConfigs" to "mixins.cero.json",
            "Implementation-Version" to project.version.toString()
        )
    }

    relocate("com.google.common", "fr.cerostudio.libs.guava")
    relocate("com.google.gson", "fr.cerostudio.libs.gson")
    relocate("org.objectweb.asm", "fr.cerostudio.libs.asm")

    exclude("META-INF/versions/**")
    exclude("module-info.class")

    mergeServiceFiles()
}

tasks.jar {
    enabled = false
}

tasks.build {
    dependsOn(tasks.shadowJar)
}

val isWindows = System.getProperty("os.name").lowercase().contains("windows")
val launcherDir = rootDir.parentFile

val inFlatpak = !isWindows && (
        File("/.flatpak-info").exists() || System.getenv("FLATPAK_ID") != null
        )

tasks.register<Exec>("runLauncher") {
    group = "application"
    description = "Lance le launcher CeroClient complet via ../run.py"

    workingDir = launcherDir

    val python = if (isWindows) "python" else "python3"
    if (inFlatpak) {
        commandLine(
            "flatpak-spawn", "--host",
            "--directory=${launcherDir.absolutePath}",
            "--env=PYTHONUNBUFFERED=1",
            python, "-u", "run.py"
        )
    } else {
        commandLine(python, "-u", "run.py")
    }

    isIgnoreExitValue = true
    standardInput = System.`in`

    doFirst {
        val runPy = launcherDir.resolve("run.py")
        if (!runPy.exists()) {
            throw GradleException("run.py introuvable : ${runPy.absolutePath}")
        }
        println("› Lancement de ${runPy.absolutePath}" + if (inFlatpak) "  [hôte via flatpak-spawn]" else "")
    }

    doLast {
        val code = executionResult.get().exitValue
        if (code != 0) {
            throw GradleException(
                "run.py a échoué (code $code). Regarde la ligne '✗ ...' juste au-dessus pour la cause."
            )
        }
    }
}

val testVersions = listOf(
    "1.7.10", "1.8.9", "1.9.4", "1.10.2", "1.11.2", "1.12.2",
    "1.13.2", "1.14.4", "1.15.2", "1.16.5", "1.17.1", "1.18.2",
    "1.19.4", "1.20.1", "1.20.4", "1.21.1", "1.21.4", "1.21.11",
)

fun runPyCommand(vararg args: String): List<String> {
    val python = if (isWindows) "python" else "python3"
    return if (inFlatpak) {
        listOf(
            "flatpak-spawn", "--host",
            "--directory=${launcherDir.absolutePath}",
            "--env=PYTHONUNBUFFERED=1",
            python, "-u"
        ) + args
    } else {
        listOf(python, "-u") + args
    }
}

fun pickVersionDialog(): String? {
    if (GraphicsEnvironment.isHeadless()) return null
    return JOptionPane.showInputDialog(
        null,
        "Version à lancer :",
        "CeroClient - launchGame",
        JOptionPane.QUESTION_MESSAGE,
        null,
        testVersions.toTypedArray(),
        testVersions.first()
    ) as String?
}

val mcVersionProp: String? = providers.gradleProperty("mcVersion").orNull

fun registerLaunch(taskName: String, taskGroup: String, fixedVersion: String?) =
    tasks.register<Exec>(taskName) {
        group = taskGroup
        description = if (fixedVersion != null)
            "Lance Minecraft $fixedVersion avec l'agent CeroClient (sans UI)"
        else
            "Lance Minecraft avec l'agent CeroClient (sans UI). Version : menu ou -PmcVersion=<v>"

        workingDir = launcherDir
        isIgnoreExitValue = true
        standardInput = System.`in`

        doFirst {
            val runPy = launcherDir.resolve("run.py")
            if (!runPy.exists()) {
                throw GradleException("run.py introuvable : ${runPy.absolutePath}")
            }

            val version = fixedVersion
                ?: mcVersionProp
                ?: pickVersionDialog()
                ?: throw GradleException(
                    "Aucune version choisie. Utilise -PmcVersion=<version>, " +
                            "une tâche 'launch-<version>', ou active le menu (headless=false)."
                )

            if (version !in testVersions) {
                logger.warn("⚠ '$version' n'est pas dans testVersions (lancement quand même)")
            }

            commandLine(runPyCommand("run.py", "--launch=$version"))
            println("› Lancement de Minecraft $version via ${runPy.absolutePath}" +
                    if (inFlatpak) "  [hôte via flatpak-spawn]" else "")
        }

        doLast {
            val code = executionResult.get().exitValue
            if (code != 0) {
                throw GradleException(
                    "Le lancement a échoué (code $code). Regarde la ligne '✗ ...' ou les logs juste au-dessus."
                )
            }
        }
    }

registerLaunch("launchGame", "application", null)

testVersions.forEach { v -> registerLaunch("launch-$v", "cero versions", v) }