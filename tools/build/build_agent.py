import os
import re
import sys
import glob
import shutil
import hashlib
import subprocess
from logger import step, ok, info, warn_, fail_

IS_WINDOWS = sys.platform == "win32"
IS_MACOS = sys.platform == "darwin"
EXE = ".exe" if IS_WINDOWS else ""

def compute_source_hash(agent_dir, java_home=""):
    hasher = hashlib.sha256()
    files = []
    for root, _, filenames in os.walk(os.path.join(agent_dir, "src")):
        for f in filenames:
            if f.endswith((".java", ".kt", ".json", ".toml")):
                files.append(os.path.join(root, f))
    for f in sorted(files):
        hasher.update(os.path.relpath(f, agent_dir).encode())
        with open(f, "rb") as fh:
            hasher.update(fh.read())
    for cfg in ("build.gradle", "build.gradle.kts", "settings.gradle",
                "settings.gradle.kts", "gradle.properties",
                os.path.join("gradle", "wrapper", "gradle-wrapper.properties")):
        path = os.path.join(agent_dir, cfg)
        if os.path.exists(path):
            with open(path, "rb") as fh:
                hasher.update(fh.read())
    hasher.update(java_home.encode())
    return hasher.hexdigest()


def _hash_file(agent_dir):
    return os.path.join(agent_dir, "build", ".source_hash")


def is_agent_up_to_date(agent_dir, jar_path, java_home):
    if not jar_path or not os.path.exists(jar_path):
        return False
    hf = _hash_file(agent_dir)
    if not os.path.exists(hf):
        return False
    with open(hf) as f:
        return f.read().strip() == compute_source_hash(agent_dir, java_home)


def save_source_hash(agent_dir, java_home):
    hf = _hash_file(agent_dir)
    os.makedirs(os.path.dirname(hf), exist_ok=True)
    with open(hf, "w") as f:
        f.write(compute_source_hash(agent_dir, java_home))

def find_built_jar(agent_dir):
    libs_dir = os.path.join(agent_dir, "build", "libs")
    jars = [j for j in glob.glob(os.path.join(libs_dir, "*.jar"))
            if not j.endswith(("-sources.jar", "-javadoc.jar", "-plain.jar"))]
    if not jars:
        jars = glob.glob(os.path.join(agent_dir, "*.jar"))
    return max(jars, key=os.path.getmtime) if jars else None


def copy_jar_to_assets(jar_path, assets_dir):
    dest_dir = os.path.join(assets_dir, "agent")
    os.makedirs(dest_dir, exist_ok=True)
    dest = os.path.join(dest_dir, "CeroClient-MC.jar")
    shutil.copy2(jar_path, dest)
    return dest

def parse_java_major(version):
    """'1.8.0_504' -> 8, '25.0.4.1' -> 25, '17' -> 17, '1_8' -> 8."""
    if not version:
        return None
    v = version.strip().strip("\"'").replace("_", ".")
    m = re.match(r"(\d+)(?:\.(\d+))?", v)
    if not m:
        return None
    major = int(m.group(1))
    if major == 1 and m.group(2):
        major = int(m.group(2))
    return major


def read_jdk_version(home):
    """Lit la vraie version : fichier 'release', sinon 'java -version'."""
    release = os.path.join(home, "release")
    if os.path.isfile(release):
        try:
            with open(release, encoding="utf-8", errors="ignore") as f:
                m = re.search(r'^JAVA_VERSION="?([^"\n]+)"?', f.read(), re.M)
            if m:
                return m.group(1)
        except OSError:
            pass

    java = os.path.join(home, "bin", "java" + EXE)
    if os.path.isfile(java):
        try:
            out = subprocess.run([java, "-version"], capture_output=True,
                                 text=True, timeout=15)
            m = re.search(r'version "([^"]+)"', out.stderr + out.stdout)
            if m:
                return m.group(1)
        except (OSError, subprocess.SubprocessError):
            pass
    return None

GRADLE_JAVA_MAX = [
    ((9, 1), 25), ((8, 14), 24), ((8, 10), 23), ((8, 8), 22), ((8, 5), 21),
    ((8, 3), 20), ((7, 6), 19), ((7, 5), 18), ((7, 3), 17), ((7, 0), 16),
    ((6, 7), 15), ((6, 3), 14), ((6, 0), 13), ((5, 4), 12), ((5, 0), 11),
    ((4, 7), 10), ((4, 3), 9),
]


def read_gradle_version(agent_dir):
    props = os.path.join(agent_dir, "gradle", "wrapper", "gradle-wrapper.properties")
    if not os.path.isfile(props):
        return None
    with open(props, encoding="utf-8", errors="ignore") as f:
        m = re.search(r"gradle-(\d+)\.(\d+)(?:\.(\d+))?", f.read())
    if not m:
        return None
    return tuple(int(x) for x in m.groups() if x is not None)


def gradle_java_range(gradle_version):
    if gradle_version is None:
        return 8, 21
    gv = gradle_version[:2]
    java_min = 17 if gv >= (9, 0) else 8
    java_max = 8
    for min_gradle, jmax in GRADLE_JAVA_MAX:
        if gv >= min_gradle:
            java_max = jmax
            break
    return java_min, java_max

def detect_required_java(agent_dir):
    """Renvoie (version, raison) ou (None, None)."""
    text = ""
    for name in ("build.gradle", "build.gradle.kts"):
        p = os.path.join(agent_dir, name)
        if os.path.isfile(p):
            with open(p, encoding="utf-8", errors="ignore") as f:
                text += f.read() + "\n"
    if not text:
        return None, None

    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)

    m = re.search(r"JavaLanguageVersion\.of\(\s*['\"]?(\d+)", text)
    if m:
        return int(m.group(1)), "toolchain"

    m = re.search(r"release\s*(?:=|\.set\()\s*['\"]?(\d+)", text)
    if m:
        return int(m.group(1)), "options.release"

    for key in ("sourceCompatibility", "targetCompatibility"):
        m = re.search(key + r"\s*(?:=|\.set\()\s*(?:JavaVersion\.VERSION_)?['\"]?([\d._]+)", text)
        if m:
            v = parse_java_major(m.group(1))
            if v:
                return v, key

    if re.search(r"ForgeGradle:[12]\.", text):
        return 8, "ForgeGradle 1.x/2.x"

    return None, None

def _candidate_dirs():
    home = os.path.expanduser("~")
    dirs = []

    for var in ("CERO_JAVA_HOME", "JAVA_HOME", "JDK_HOME"):
        v = os.environ.get(var, "").strip()
        if v:
            dirs.append((v, var))
    for var, val in os.environ.items():
        if re.fullmatch(r"JAVA_?\d+_HOME|JDK_?\d+_HOME", var) and val.strip():
            dirs.append((val.strip(), var))

    patterns = [
        ("/usr/lib/jvm/*", "système"),
        ("/usr/lib64/jvm/*", "système"),
        ("/usr/java/*", "système"),
        ("/usr/local/openjdk*", "système"),
        ("/usr/local/lib/jvm/*", "système"),
        ("/opt/*jdk*", "/opt"),
        ("/opt/java/*", "/opt"),
        ("/usr/local/*jdk*", "/usr/local"),
        (os.path.join(home, ".sdkman", "candidates", "java", "*"), "SDKMAN"),
        (os.path.join(home, ".asdf", "installs", "java", "*"), "asdf"),
        (os.path.join(home, ".local", "share", "mise", "installs", "java", "*"), "mise"),
        (os.path.join(home, ".jdks", "*"), "IntelliJ"),
        (os.path.join(home, ".gradle", "jdks", "*"), "Gradle"),
        (os.path.join(home, ".cache", "cero-client", "jdks", "*"), "cache"),
    ]
    if IS_MACOS:
        patterns += [
            ("/Library/Java/JavaVirtualMachines/*/Contents/Home", "macOS"),
            (os.path.join(home, "Library", "Java", "JavaVirtualMachines", "*", "Contents", "Home"), "macOS"),
            ("/opt/homebrew/opt/openjdk*/libexec/openjdk.jdk/Contents/Home", "Homebrew"),
            ("/usr/local/opt/openjdk*/libexec/openjdk.jdk/Contents/Home", "Homebrew"),
        ]
    if IS_WINDOWS:
        for pf in {os.environ.get("ProgramFiles", r"C:\Program Files"),
                   os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")}:
            for vendor in ("Java", "Eclipse Adoptium", "Zulu", "Microsoft",
                           "BellSoft", "Amazon Corretto", "OpenJDK", "Semeru"):
                patterns.append((os.path.join(pf, vendor, "*"), vendor))

    for pattern, source in patterns:
        for d in sorted(glob.glob(pattern)):
            dirs.append((d, source))

    if IS_MACOS and os.path.exists("/usr/libexec/java_home"):
        try:
            out = subprocess.run(["/usr/libexec/java_home", "-V"],
                                 capture_output=True, text=True, timeout=10)
            for line in (out.stderr + out.stdout).splitlines():
                m = re.search(r"(/\S.*/Contents/Home)\s*$", line)
                if m:
                    dirs.append((m.group(1), "java_home"))
        except (OSError, subprocess.SubprocessError):
            pass

    for exe in ("javac", "java"):
        p = shutil.which(exe)
        if p:
            dirs.append((os.path.dirname(os.path.dirname(os.path.realpath(p))), "PATH"))

    return dirs


def discover_jdks():
    """Liste de dicts {home, version, major, has_javac, source}, dédoublonnée."""
    seen = set()
    jdks = []
    for d, source in _candidate_dirs():
        if not os.path.isdir(d):
            continue
        if os.path.basename(d.rstrip("/\\")) == "jre" and \
                os.path.isfile(os.path.join(os.path.dirname(d), "bin", "javac" + EXE)):
            d = os.path.dirname(d)
        real = os.path.realpath(d)
        if real in seen:
            continue
        if not os.path.isfile(os.path.join(real, "bin", "java" + EXE)):
            continue
        seen.add(real)
        version = read_jdk_version(real)
        major = parse_java_major(version)
        if major is None:
            continue
        jdks.append({
            "home": real,
            "version": version,
            "major": major,
            "has_javac": os.path.isfile(os.path.join(real, "bin", "javac" + EXE)),
            "source": source,
        })
    return jdks


def describe(j):
    kind = "JDK" if j["has_javac"] else "JRE seul"
    return f"Java {j['major']} ({j['version']}, {kind}) {j['home']}  [{j['source']}]"

def install_hint(version):
    if shutil.which("pacman"):
        return f"sudo pacman -S jdk{version}-openjdk" if version != 8 else "sudo pacman -S jdk8-openjdk"
    if shutil.which("apt"):
        return f"sudo apt install openjdk-{version}-jdk"
    if shutil.which("dnf"):
        return f"sudo dnf install java-{'1.8.0' if version == 8 else version}-openjdk-devel"
    if shutil.which("zypper"):
        return f"sudo zypper install java-{'1_8_0' if version == 8 else version}-openjdk-devel"
    if shutil.which("brew"):
        return f"brew install openjdk@{version}"
    if IS_WINDOWS:
        return f"winget install EclipseAdoptium.Temurin.{version}.JDK"
    return f"installer un JDK {version} (https://adoptium.net)"


def select_jdk(agent_dir):
    gradle_version = read_gradle_version(agent_dir)
    run_min, run_max = gradle_java_range(gradle_version)
    gv_str = ".".join(map(str, gradle_version)) if gradle_version else "inconnue"
    info(f"Gradle {gv_str} -> Java {run_min} à {run_max}")

    required, reason = detect_required_java(agent_dir)
    if required:
        info(f"{reason} -> Java {required}")

    jdks = discover_jdks()

    forced = os.environ.get("CERO_JAVA_HOME", "").strip()
    if forced:
        real = os.path.realpath(forced)
        match = next((j for j in jdks if j["home"] == real), None)
        if match is None:
            fail_(f"CERO_JAVA_HOME={forced} n'est pas un JDK valide.")
        if not (run_min <= match["major"] <= run_max):
            warn_(f"Java {match['major']} forcé, mais Gradle {gv_str} supporte Java {run_min}-{run_max}.")
        return match

    lower = max(run_min, required or run_min)
    compatible = [j for j in jdks
                  if j["has_javac"] and lower <= j["major"] <= run_max]

    if compatible:
        if required:
            compatible.sort(key=lambda j: (j["major"] != required, j["major"]))
        else:
            compatible.sort(key=lambda j: -j["major"])
        return compatible[0]

    print()
    warn_("Aucun JDK compatible trouvé.")
    if jdks:
        info("JDK détectés :")
        for j in jdks:
            print(f"      ✗ {describe(j)}")
    else:
        info("Aucune installation Java détectée.")
    target = required if required and run_min <= required <= run_max else min(run_max, 21)
    info(f"Installe un JDK {target} : {install_hint(target)}")
    info("Ou force un JDK : CERO_JAVA_HOME=/chemin/vers/jdk python3 run.py")
    fail_("Impossible de continuer sans JDK compatible.")


def check_gradle_properties(agent_dir):
    gradle_user_home = os.environ.get("GRADLE_USER_HOME",
                                      os.path.join(os.path.expanduser("~"), ".gradle"))
    for p in (os.path.join(agent_dir, "gradle.properties"),
              os.path.join(gradle_user_home, "gradle.properties")):
        if not os.path.isfile(p):
            continue
        with open(p, encoding="utf-8", errors="ignore") as f:
            m = re.search(r"^\s*org\.gradle\.java\.home\s*=\s*(.+)$", f.read(), re.M)
        if m:
            path = m.group(1).strip().replace("\\\\", "\\")
            if not os.path.isdir(path):
                warn_(f"{p} : org.gradle.java.home={path} n'existe pas (ignoré, surchargé).")
            else:
                info(f"{p} définit org.gradle.java.home (surchargé par le JDK sélectionné).")


def build_env(jdk):
    env = os.environ.copy()
    home = jdk["home"]
    env["JAVA_HOME"] = home
    env["PATH"] = os.path.join(home, "bin") + os.pathsep + env.get("PATH", "")
    for var in ("JDK_JAVA_OPTIONS",):
        env.pop(var, None)
    gradle_args = [
        f"-Dorg.gradle.java.home={home}",
        f"-Porg.gradle.java.installations.paths={home}",
    ]
    return env, gradle_args

def run():
    step("Building Minecraft Agent (Java)...")

    agent_dir = os.path.abspath("agent")
    assets_dir = os.path.abspath("assets")

    if not os.path.isdir(agent_dir):
        fail_("Agent directory not found.")

    gradlew_name = "gradlew.bat" if IS_WINDOWS else "gradlew"
    gradlew_path = os.path.join(agent_dir, gradlew_name)
    if not os.path.exists(gradlew_path):
        fail_(f"{gradlew_name} not found in {agent_dir}")
    if not IS_WINDOWS:
        os.chmod(gradlew_path, 0o755)

    jdk = select_jdk(agent_dir)
    ok(f"JDK sélectionné : {describe(jdk)}")
    check_gradle_properties(agent_dir)

    jar_path = find_built_jar(agent_dir)
    if is_agent_up_to_date(agent_dir, jar_path, jdk["home"]):
        ok(f"Agent is already up-to-date (JAR: {os.path.basename(jar_path)}). Skipping Gradle build.")
        copy_jar_to_assets(jar_path, assets_dir)
        return

    info("Sources have changed, running Gradle build...")

    env, gradle_args = build_env(jdk)
    cmd = [gradlew_path, *gradle_args, "build", "--no-daemon"]
    try:
        result = subprocess.run(cmd, cwd=agent_dir, env=env)
    except OSError as e:
        fail_(f"Impossible de lancer {gradlew_name} : {e}")

    if result.returncode != 0:
        fail_(f"Agent build failed with Java {jdk['major']} ({jdk['home']}). "
              f"Relance avec : cd agent && JAVA_HOME={jdk['home']} ./{gradlew_name} build --stacktrace")

    jar_path = find_built_jar(agent_dir)
    if not jar_path:
        fail_("Build succeeded but no JAR was found in build/libs/.")

    copy_jar_to_assets(jar_path, assets_dir)
    save_source_hash(agent_dir, jdk["home"])
    ok(f"Agent built successfully ({os.path.basename(jar_path)})")