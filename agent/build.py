#!/usr/bin/env python3
"""
Build robuste de l'agent Java CeroClient.

Usage :
  python3 build_agent.py                 # build normal
  python3 build_agent.py --list          # liste les JDK détectés
  python3 build_agent.py --java /chemin  # force un JDK
  python3 build_agent.py --java 8        # force une version majeure
  python3 build_agent.py --download      # autorise le téléchargement auto sans demander
  python3 build_agent.py clean build     # tâches Gradle personnalisées
  python3 build_agent.py --stacktrace    # les options inconnues sont passées à Gradle

Variables d'environnement :
  CERO_JAVA_HOME      JDK forcé (prioritaire sur tout)
  CERO_AUTO_DOWNLOAD  "1" pour télécharger un JDK sans confirmation
"""

from __future__ import annotations

import glob
import os
import platform
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Optional

# --------------------------------------------------------------------------- #
#  Constantes / plateforme
# --------------------------------------------------------------------------- #

SYSTEM = platform.system()
IS_WINDOWS = SYSTEM == "Windows"
IS_MAC = SYSTEM == "Darwin"
EXE = ".exe" if IS_WINDOWS else ""
HOME = Path.home()
CACHE_DIR = Path(os.environ.get("XDG_CACHE_HOME", HOME / ".cache")) / "cero-client" / "jdks"

# Matrice officielle : version de Gradle minimale -> Java max supporté POUR FAIRE TOURNER Gradle
# https://docs.gradle.org/current/userguide/compatibility.html
GRADLE_MAX_JAVA = [
    ((9, 1), 25),
    ((8, 14), 24),
    ((8, 10), 23),
    ((8, 8), 22),
    ((8, 5), 21),
    ((8, 3), 20),
    ((7, 6), 19),
    ((7, 5), 18),
    ((7, 3), 17),
    ((7, 0), 16),
    ((6, 7), 15),
    ((6, 3), 14),
    ((6, 0), 13),
    ((5, 4), 12),
    ((5, 0), 11),
    ((4, 7), 10),
    ((4, 3), 9),
    ((0, 0), 8),
]

# --------------------------------------------------------------------------- #
#  Affichage
# --------------------------------------------------------------------------- #

def _c(code: str) -> str:
    return f"\033[{code}m" if sys.stdout.isatty() and not IS_WINDOWS else ""

RESET, BOLD, RED, GREEN, YELLOW, CYAN, DIM = (
    _c("0"), _c("1"), _c("31"), _c("32"), _c("33"), _c("36"), _c("2")
)

def step(msg): print(f"\n{BOLD}» {msg}{RESET}")
def ok(msg):   print(f"  {GREEN}✓{RESET} {msg}")
def info(msg): print(f"  {CYAN}›{RESET} {msg}")
def warn(msg): print(f"  {YELLOW}!{RESET} {msg}")
def err(msg):  print(f"  {RED}✗{RESET} {msg}")

# --------------------------------------------------------------------------- #
#  Modèle
# --------------------------------------------------------------------------- #

@dataclass
class JDK:
    home: Path
    major: int
    version: str
    has_javac: bool
    source: str
    vendor: str = ""

    def __str__(self):
        kind = "JDK" if self.has_javac else "JRE"
        v = f" {self.vendor}" if self.vendor else ""
        return f"Java {self.major:<3} ({self.version}{v}, {kind}) {self.home}  {DIM}[{self.source}]{RESET}"


@dataclass
class Requirements:
    gradle_version: Optional[tuple] = None
    run_min: int = 8
    run_max: int = 99
    project_java: Optional[int] = None
    strict: bool = False           # True = version exacte obligatoire (ex: ForgeGradle 2)
    uses_toolchain: bool = False
    reasons: list = field(default_factory=list)

# --------------------------------------------------------------------------- #
#  Parsing des versions
# --------------------------------------------------------------------------- #

def parse_major(version: str) -> Optional[int]:
    version = version.strip().strip('"')
    m = re.match(r"^1\.(\d+)", version)
    if m:
        return int(m.group(1))
    m = re.match(r"^(\d+)", version)
    return int(m.group(1)) if m else None


def read_release_file(home: Path) -> tuple[Optional[str], str]:
    rel = home / "release"
    if not rel.is_file():
        return None, ""
    version, vendor = None, ""
    try:
        for line in rel.read_text(errors="ignore").splitlines():
            if line.startswith("JAVA_VERSION="):
                version = line.split("=", 1)[1].strip().strip('"')
            elif line.startswith("IMPLEMENTOR="):
                vendor = line.split("=", 1)[1].strip().strip('"')
    except OSError:
        pass
    return version, vendor


def query_java_binary(home: Path) -> Optional[str]:
    java = home / "bin" / f"java{EXE}"
    if not java.is_file():
        return None
    try:
        out = subprocess.run(
            [str(java), "-version"], capture_output=True, text=True, timeout=15
        )
        text = out.stderr + out.stdout
        m = re.search(r'version "([^"]+)"', text)
        return m.group(1) if m else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def probe_jdk(path: Path, source: str) -> Optional[JDK]:
    """Transforme un chemin quelconque en JDK valide (ou None)."""
    try:
        path = Path(path).expanduser().resolve()
    except OSError:
        return None

    # macOS : bundle .jdk -> Contents/Home
    if (path / "Contents" / "Home").is_dir():
        path = path / "Contents" / "Home"
    # Chemin pointant vers bin/ ou bin/java
    if path.name in ("java", f"java{EXE}", "javac", f"javac{EXE}"):
        path = path.parent
    if path.name == "bin":
        path = path.parent
    # JRE embarqué d'un JDK 8 (jdk/jre) -> remonter au JDK
    if path.name == "jre" and (path.parent / "bin" / f"javac{EXE}").is_file():
        path = path.parent

    if not (path / "bin" / f"java{EXE}").is_file():
        return None

    version, vendor = read_release_file(path)
    if not version:
        version = query_java_binary(path)
    if not version:
        return None
    major = parse_major(version)
    if major is None:
        return None

    return JDK(
        home=path,
        major=major,
        version=version,
        has_javac=(path / "bin" / f"javac{EXE}").is_file(),
        source=source,
        vendor=vendor,
    )

# --------------------------------------------------------------------------- #
#  Découverte des JDK
# --------------------------------------------------------------------------- #

def _children(*patterns: str) -> Iterable[str]:
    for p in patterns:
        yield from glob.glob(os.path.expanduser(p))


def _windows_registry_homes() -> Iterable[str]:
    try:
        import winreg  # type: ignore
    except ImportError:
        return
    roots = [
        r"SOFTWARE\JavaSoft\JDK",
        r"SOFTWARE\JavaSoft\Java Development Kit",
        r"SOFTWARE\JavaSoft\Java Runtime Environment",
        r"SOFTWARE\Eclipse Adoptium\JDK",
        r"SOFTWARE\Eclipse Foundation\JDK",
        r"SOFTWARE\Azul Systems\Zulu",
        r"SOFTWARE\Microsoft\JDK",
        r"SOFTWARE\Amazon Corretto",
        r"SOFTWARE\BellSoft\Liberica",
    ]
    for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
        for root in roots:
            for view in (0, winreg.KEY_WOW64_64KEY, winreg.KEY_WOW64_32KEY):
                try:
                    key = winreg.OpenKey(hive, root, 0, winreg.KEY_READ | view)
                except OSError:
                    continue
                yield from _walk_registry(winreg, key, depth=0)


def _walk_registry(winreg, key, depth):
    for name in ("JavaHome", "Path", "InstallationPath"):
        try:
            val, _ = winreg.QueryValueEx(key, name)
            if val:
                yield val
        except OSError:
            pass
    if depth > 3:
        return
    i = 0
    while True:
        try:
            sub = winreg.EnumKey(key, i)
        except OSError:
            break
        i += 1
        try:
            yield from _walk_registry(winreg, winreg.OpenKey(key, sub), depth + 1)
        except OSError:
            continue


def candidate_paths() -> Iterable[tuple[str, str]]:
    """Génère (chemin, source) pour tous les emplacements connus."""
    for var in ("CERO_JAVA_HOME", "JAVA_HOME", "JDK_HOME", "JAVA8_HOME",
                "JAVA_HOME_8_X64", "JAVA_HOME_17_X64", "JAVA_HOME_21_X64"):
        v = os.environ.get(var, "").strip()
        if v:
            yield v, f"${var}"

    # Toutes les variables JAVA_HOME_* (CI GitHub, etc.)
    for k, v in os.environ.items():
        if k.startswith("JAVA_HOME_") and v:
            yield v, f"${k}"

    # Linux / BSD
    for p in _children("/usr/lib/jvm/*", "/usr/lib64/jvm/*", "/usr/java/*",
                       "/usr/local/openjdk*", "/usr/local/jdk*", "/usr/local/lib/jvm/*",
                       "/opt/java/*", "/opt/jdk*", "/opt/*jdk*", "/opt/openjdk*"):
        yield p, "système"

    # Gestionnaires de versions utilisateur
    for p in _children("~/.sdkman/candidates/java/*",
                       "~/.asdf/installs/java/*",
                       "~/.local/share/mise/installs/java/*",
                       "~/.jabba/jdk/*",
                       "~/.jdks/*",
                       "~/.gradle/jdks/*",
                       "~/.gradle/jdks/*/*",
                       str(CACHE_DIR / "*"),
                       str(CACHE_DIR / "*" / "*")):
        yield p, "utilisateur"

    # macOS
    if IS_MAC:
        for p in _children("/Library/Java/JavaVirtualMachines/*",
                           "~/Library/Java/JavaVirtualMachines/*",
                           "/opt/homebrew/opt/openjdk*",
                           "/usr/local/opt/openjdk*",
                           "/opt/homebrew/Cellar/openjdk*/*"):
            yield p, "macOS"
        try:
            out = subprocess.run(["/usr/libexec/java_home", "-V"],
                                 capture_output=True, text=True, timeout=10)
            for line in (out.stderr + out.stdout).splitlines():
                m = re.search(r"(/\S.*?)$", line.strip())
                if m and os.path.isdir(m.group(1)):
                    yield m.group(1), "java_home"
        except (OSError, subprocess.TimeoutExpired):
            pass

    # Windows
    if IS_WINDOWS:
        for base in (os.environ.get("ProgramFiles", r"C:\Program Files"),
                     os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                     os.environ.get("LOCALAPPDATA", "")):
            if not base:
                continue
            for vendor in ("Java", "Eclipse Adoptium", "Eclipse Foundation", "AdoptOpenJDK",
                           "Zulu", "Microsoft", "Amazon Corretto", "BellSoft",
                           "Semeru", "OpenJDK", "RedHat", "Programs\\Eclipse Adoptium"):
                for p in glob.glob(os.path.join(base, vendor, "*")):
                    yield p, "Program Files"
        for p in _windows_registry_homes():
            yield p, "registre"
        for p in _children("~/scoop/apps/*jdk*/*", "~/scoop/apps/*java*/*"):
            yield p, "scoop"

    # PATH
    for exe in ("javac", "java"):
        w = shutil.which(exe)
        if w:
            yield os.path.realpath(w), "PATH"


def discover_jdks() -> list[JDK]:
    seen: dict[Path, JDK] = {}
    for raw, source in candidate_paths():
        jdk = probe_jdk(Path(raw), source)
        if jdk and jdk.home not in seen:
            seen[jdk.home] = jdk
    return sorted(seen.values(), key=lambda j: (j.major, j.has_javac), reverse=True)

# --------------------------------------------------------------------------- #
#  Analyse du projet
# --------------------------------------------------------------------------- #

def read_text(p: Path) -> str:
    try:
        return p.read_text(errors="ignore")
    except OSError:
        return ""


def gradle_version(project: Path) -> Optional[tuple]:
    props = read_text(project / "gradle" / "wrapper" / "gradle-wrapper.properties")
    m = re.search(r"gradle-(\d+)\.(\d+)(?:\.(\d+))?(?:-[\w.-]+)?-(?:bin|all)\.zip", props)
    if not m:
        return None
    return tuple(int(x) if x else 0 for x in m.groups())


def analyze_project(project: Path) -> Requirements:
    req = Requirements()

    # Gradle -> plage Java d'exécution
    gv = gradle_version(project)
    req.gradle_version = gv
    if gv:
        for min_gv, max_java in GRADLE_MAX_JAVA:
            if gv[:2] >= min_gv:
                req.run_max = max_java
                break
        if gv[0] >= 9:
            req.run_min = 17
        req.reasons.append(
            f"Gradle {'.'.join(map(str, gv))} → Java {req.run_min} à {req.run_max}"
        )
    else:
        warn("Version de Gradle introuvable dans gradle-wrapper.properties")

    # Fichiers de build
    text = ""
    for name in ("build.gradle", "build.gradle.kts", "settings.gradle",
                 "settings.gradle.kts", "gradle.properties"):
        text += "\n" + read_text(project / name)

    # ForgeGradle 1.x / 2.x => Java 8 obligatoire
    if re.search(r"ForgeGradle:[12]\.", text) or \
       re.search(r"net\.minecraftforge\.gradle\.forge", text) or \
       re.search(r"id\s*[\(\"']+net\.minecraftforge\.gradle\.forge", text):
        req.project_java, req.strict = 8, True
        req.reasons.append("ForgeGradle 1.x/2.x → Java 8 exact")

    # Toolchain
    m = re.search(r"JavaLanguageVersion\.of\(\s*(\d+)\s*\)", text)
    if m:
        req.uses_toolchain = True
        if req.project_java is None:
            req.project_java = int(m.group(1))
            req.reasons.append(f"toolchain → Java {req.project_java}")

    # options.release
    m = re.search(r"options\.release(?:\.set\()?\s*=?\s*\(?\s*(\d+)", text)
    if m and req.project_java is None:
        req.project_java = int(m.group(1))
        req.reasons.append(f"options.release → Java {req.project_java}")

    # sourceCompatibility / targetCompatibility
    m = re.search(
        r"(?:source|target)Compatibility\s*=\s*"
        r"(?:JavaVersion\.VERSION_)?['\"]?(1[._]\d+|\d+)['\"]?", text)
    if m and req.project_java is None:
        req.project_java = parse_major(m.group(1).replace("_", "."))
        req.reasons.append(f"sourceCompatibility → Java {req.project_java}")

    return req

# --------------------------------------------------------------------------- #
#  Sélection
# --------------------------------------------------------------------------- #

def select_jdk(jdks: list[JDK], req: Requirements, forced: Optional[str]) -> Optional[JDK]:
    usable = [j for j in jdks if j.has_javac]

    # Choix forcé par l'utilisateur
    if forced:
        if forced.isdigit():
            match = [j for j in usable if j.major == int(forced)]
            if match:
                return match[0]
            err(f"Aucun JDK {forced} trouvé.")
            return None
        j = probe_jdk(Path(forced), "--java")
        if not j:
            err(f"{forced} n'est pas un JDK valide.")
        return j

    runnable = [j for j in usable if req.run_min <= j.major <= req.run_max]

    # 1. Version exacte du projet, compatible avec Gradle
    if req.project_java:
        exact = [j for j in runnable if j.major == req.project_java]
        if exact:
            return exact[0]
        if req.strict:
            return None

    # 2. Toolchain : n'importe quel JDK capable de lancer Gradle (le toolchain fera le reste)
    if req.uses_toolchain and runnable:
        return runnable[0]

    # 3. Le plus récent compatible, et >= version projet
    floor = req.project_java or 0
    compatible = [j for j in runnable if j.major >= floor]
    if compatible:
        return compatible[0]

    return None


def target_version(req: Requirements) -> int:
    if req.project_java and req.run_min <= req.project_java <= req.run_max:
        return req.project_java
    # LTS la plus haute supportée
    for lts in (21, 17, 11, 8):
        if req.run_min <= lts <= req.run_max:
            return lts
    return req.run_max

# --------------------------------------------------------------------------- #
#  Téléchargement automatique (Eclipse Temurin via l'API Adoptium)
# --------------------------------------------------------------------------- #

def adoptium_platform() -> tuple[str, str]:
    os_name = {"Linux": "linux", "Darwin": "mac", "Windows": "windows"}.get(SYSTEM, "linux")
    mach = platform.machine().lower()
    arch = {
        "x86_64": "x64", "amd64": "x64",
        "aarch64": "aarch64", "arm64": "aarch64",
        "armv7l": "arm", "i386": "x86-32", "i686": "x86-32",
    }.get(mach, "x64")
    # Pas de Java 8 natif Apple Silicon chez Temurin : on prend x64 (Rosetta)
    return os_name, arch


def download_jdk(major: int) -> Optional[JDK]:
    os_name, arch = adoptium_platform()
    if os_name == "mac" and arch == "aarch64" and major == 8:
        arch = "x64"
    url = (f"https://api.adoptium.net/v3/binary/latest/{major}/ga/"
           f"{os_name}/{arch}/jdk/hotspot/normal/eclipse")
    dest = CACHE_DIR / f"temurin-{major}"
    info(f"Téléchargement de Temurin {major} ({os_name}/{arch})...")
    info(url)

    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    suffix = ".zip" if os_name == "windows" else ".tar.gz"
    try:
        with tempfile.NamedTemporaryFile(suffix=suffix, delete=False) as tmp:
            req = urllib.request.Request(url, headers={"User-Agent": "CeroClient-build"})
            with urllib.request.urlopen(req, timeout=60) as resp:
                total = int(resp.headers.get("Content-Length", 0))
                done = 0
                while chunk := resp.read(1 << 16):
                    tmp.write(chunk)
                    done += len(chunk)
                    if total and sys.stdout.isatty():
                        print(f"\r    {done * 100 // total:3d}%  "
                              f"{done >> 20}/{total >> 20} MiB", end="", flush=True)
            if sys.stdout.isatty():
                print()
            archive = tmp.name

        if dest.exists():
            shutil.rmtree(dest)
        dest.mkdir(parents=True)
        info("Extraction...")
        if suffix == ".zip":
            with zipfile.ZipFile(archive) as z:
                z.extractall(dest)
        else:
            with tarfile.open(archive) as t:
                if sys.version_info >= (3, 12):
                    t.extractall(dest, filter="tar")
                else:
                    t.extractall(dest)
        os.unlink(archive)
    except Exception as e:  # noqa: BLE001
        err(f"Échec du téléchargement : {e}")
        return None

    for javac in dest.rglob(f"javac{EXE}"):
        jdk = probe_jdk(javac.parent.parent, "téléchargé")
        if jdk:
            ok(f"JDK installé dans {jdk.home}")
            return jdk
    err("Archive téléchargée mais aucun JDK trouvé dedans.")
    return None


def install_hint(major: int) -> str:
    if IS_WINDOWS:
        return f"winget install EclipseAdoptium.Temurin.{major}.JDK"
    if IS_MAC:
        return f"brew install --cask temurin@{major}"
    osr = read_text(Path("/etc/os-release")).lower()
    if any(d in osr for d in ("arch", "endeavour", "manjaro", "cachyos", "garuda")):
        pkg = "jdk8-openjdk" if major == 8 else f"jdk{major}-openjdk"
        return f"sudo pacman -S {pkg}"
    if any(d in osr for d in ("debian", "ubuntu", "mint", "pop")):
        return f"sudo apt install openjdk-{major}-jdk"
    if any(d in osr for d in ("fedora", "rhel", "centos", "rocky", "alma")):
        pkg = "java-1.8.0-openjdk-devel" if major == 8 else f"java-{major}-openjdk-devel"
        return f"sudo dnf install {pkg}"
    if "opensuse" in osr:
        pkg = "java-1_8_0-openjdk-devel" if major == 8 else f"java-{major}-openjdk-devel"
        return f"sudo zypper install {pkg}"
    if "void" in osr:
        return f"sudo xbps-install openjdk{major}"
    if "gentoo" in osr:
        return "sudo emerge dev-java/openjdk-bin"
    if "nixos" in osr:
        return f"nix-shell -p jdk{major}"
    return f"sdk install java {major}-tem   (via SDKMAN)"

# --------------------------------------------------------------------------- #
#  Vérifications diverses
# --------------------------------------------------------------------------- #

def check_gradle_properties(project: Path, req: Requirements):
    for props in (project / "gradle.properties", HOME / ".gradle" / "gradle.properties"):
        m = re.search(r"^\s*org\.gradle\.java\.home\s*=\s*(.+)$", read_text(props), re.M)
        if not m:
            continue
        path = m.group(1).strip().replace("\\\\", "\\")
        j = probe_jdk(Path(path), str(props))
        if not j:
            warn(f"{props} : org.gradle.java.home={path} est invalide (ignoré, surchargé).")
        elif not (req.run_min <= j.major <= req.run_max):
            warn(f"{props} : org.gradle.java.home pointe vers Java {j.major}, "
                 f"incompatible (surchargé en ligne de commande).")


def ensure_executable(p: Path):
    if IS_WINDOWS or not p.exists():
        return
    mode = p.stat().st_mode
    if not mode & stat.S_IXUSR:
        p.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        info(f"{p.name} rendu exécutable")

# --------------------------------------------------------------------------- #
#  API réutilisable (ex: depuis run.py)
# --------------------------------------------------------------------------- #

def resolve_java(project: Path, forced: Optional[str] = None,
                 allow_download: Optional[bool] = None,
                 quiet: bool = False) -> tuple[dict, list[str], JDK]:
    """
    Retourne (env, gradle_args, jdk) prêts à passer à subprocess.
    Lève SystemExit si aucun JDK n'est utilisable.
    """
    forced = forced or os.environ.get("CERO_JAVA_HOME") or None
    req = analyze_project(project)
    if not quiet:
        for r in req.reasons:
            info(r)

    jdks = discover_jdks()
    jdk = select_jdk(jdks, req, forced)

    if jdk is None and not forced:
        want = target_version(req)
        warn(f"Aucun JDK compatible trouvé (recherché : Java {want}).")
        if jdks:
            info("JDK détectés mais inutilisables :")
            for j in jdks:
                print(f"      {j}")
        if allow_download is None:
            allow_download = os.environ.get("CERO_AUTO_DOWNLOAD") == "1"
            if not allow_download and sys.stdin.isatty():
                try:
                    ans = input(f"  ? Télécharger Temurin {want} automatiquement "
                                f"dans {CACHE_DIR} ? [O/n] ").strip().lower()
                except EOFError:
                    ans = "n"
                allow_download = ans in ("", "o", "oui", "y", "yes")
        if allow_download:
            jdk = download_jdk(want)
        if jdk is None:
            err("Impossible de trouver un JDK utilisable.")
            print(f"\n    Installe-le avec :  {BOLD}{install_hint(want)}{RESET}")
            print(f"    Ou force un chemin : CERO_JAVA_HOME=/chemin/vers/jdk python3 {Path(sys.argv[0]).name}\n")
            raise SystemExit(1)

    if jdk is None:
        raise SystemExit(1)

    if not (req.run_min <= jdk.major <= req.run_max):
        warn(f"Java {jdk.major} est hors de la plage supportée par Gradle "
             f"({req.run_min}-{req.run_max}). Le build risque d'échouer.")

    if not quiet:
        ok(f"JDK sélectionné : {jdk}")
        check_gradle_properties(project, req)

    env = os.environ.copy()
    env["JAVA_HOME"] = str(jdk.home)
    env["PATH"] = str(jdk.home / "bin") + os.pathsep + env.get("PATH", "")
    env.pop("JDK_JAVA_OPTIONS", None)   # peut casser les vieux JDK / afficher du bruit
    env.pop("_JAVA_OPTIONS", None)

    toolchain_paths = ",".join(str(j.home) for j in jdks if j.has_javac)
    args = [
        f"-Dorg.gradle.java.home={jdk.home}",
        f"-Porg.gradle.java.installations.paths={toolchain_paths or jdk.home}",
    ]
    return env, args, jdk

# --------------------------------------------------------------------------- #
#  Main
# --------------------------------------------------------------------------- #

def main(argv: list[str]) -> int:
    project = Path(__file__).resolve().parent
    forced, allow_download, list_only, no_daemon = None, None, False, False
    tasks, extra = [], []

    it = iter(argv)
    for a in it:
        if a == "--java":
            forced = next(it, None)
        elif a.startswith("--java="):
            forced = a.split("=", 1)[1]
        elif a == "--download":
            allow_download = True
        elif a == "--no-download":
            allow_download = False
        elif a == "--list":
            list_only = True
        elif a == "--no-daemon":
            no_daemon = True
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        elif a.startswith("-"):
            extra.append(a)
        else:
            tasks.append(a)

    if list_only:
        step("JDK détectés")
        req = analyze_project(project)
        for r in req.reasons:
            info(r)
        for j in discover_jdks():
            mark = GREEN + "✓" if j.has_javac and req.run_min <= j.major <= req.run_max else RED + "✗"
            print(f"  {mark}{RESET} {j}")
        return 0

    step("Recherche d'un JDK compatible")
    env, gradle_args, jdk = resolve_java(project, forced, allow_download)

    step("Build Gradle")
    gradlew = project / ("gradlew.bat" if IS_WINDOWS else "gradlew")
    if not gradlew.exists():
        err(f"{gradlew.name} introuvable dans {project}")
        return 1
    ensure_executable(gradlew)

    cmd = [str(gradlew), *gradle_args, *(tasks or ["build"]), *extra]
    if no_daemon:
        cmd.append("--no-daemon")
    info(" ".join(cmd[:1] + (tasks or ["build"]) + extra))

    try:
        subprocess.run(cmd, env=env, cwd=project, check=True)
    except subprocess.CalledProcessError as e:
        err(f"Build échoué (code {e.returncode}) avec Java {jdk.major} ({jdk.home})")
        print(f"    Astuces : {BOLD}python3 {Path(sys.argv[0]).name} --list{RESET}  |  "
              f"{BOLD}--java 8{RESET}  |  {BOLD}--stacktrace{RESET}  |  "
              f"{BOLD}./gradlew --stop{RESET}")
        return e.returncode
    except KeyboardInterrupt:
        err("Interrompu.")
        return 130

    libs = project / "build" / "libs"
    jars = sorted(libs.glob("*.jar")) if libs.is_dir() else []
    ok("Build réussi")
    for j in jars:
        print(f"    {j.relative_to(project)}  ({j.stat().st_size // 1024} Ko)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))