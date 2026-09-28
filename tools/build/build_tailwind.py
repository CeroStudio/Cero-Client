import os
import glob
import shutil
import subprocess
import sys
from logger import step, ok, info, warn_, fail_

ASSETS_DIR = "assets"
CONFIG_PATH = "tailwind.config.js"
INPUT_CSS = os.path.join("style", "tailwind.src.css")
OUTPUT_CSS = os.path.join("style", "tailwind.css")

DEFAULT_INPUT_CSS = "@tailwind base;\n@tailwind components;\n@tailwind utilities;\n"

IS_WINDOWS = sys.platform == "win32"


def _node_cmd():
    return "node.exe" if IS_WINDOWS else "node"

def _npm_cmd():
    return "npm.cmd" if IS_WINDOWS else "npm"

def _npx_cmd():
    return "npx.cmd" if IS_WINDOWS else "npx"


def _parse_version(text):
    parts = text.strip().lstrip("v").split(".")
    if not parts or not all(p.isdigit() for p in parts):
        return None
    return tuple(int(p) for p in parts)


def _nvm_dir():
    return os.environ.get("NVM_DIR") or os.path.join(os.path.expanduser("~"), ".nvm")


def _nvm_installed(nvm_dir):
    result = []
    for path in glob.glob(os.path.join(nvm_dir, "versions", "node", "v*")):
        ver = _parse_version(os.path.basename(path))
        bin_dir = os.path.join(path, "bin")
        if ver and os.path.isdir(bin_dir):
            result.append((ver, bin_dir))
    return sorted(result, reverse=True)


def _nvm_resolve_alias(nvm_dir, alias, depth=0):
    alias = alias.strip()
    if depth > 10 or not alias:
        return None
    if alias in ("node", "stable", "current"):
        return ()

    ver = _parse_version(alias)
    if ver:
        return ver

    # Fichier d'alias : ~/.nvm/alias/default, ~/.nvm/alias/lts/*, ~/.nvm/alias/lts/krypton...
    alias_file = os.path.join(nvm_dir, "alias", *alias.split("/"))
    if os.path.isfile(alias_file):
        with open(alias_file) as f:
            return _nvm_resolve_alias(nvm_dir, f.read(), depth + 1)
    return None


def _nvm_match(installed, spec):
    for ver, bin_dir in installed:
        if ver[:len(spec)] == spec:
            return bin_dir
    return None


def _nvm_candidates():
    nvm_dir = _nvm_dir()
    installed = _nvm_installed(nvm_dir)
    if not installed:
        return []

    candidates = []

    for nvmrc in (".nvmrc", os.path.join(ASSETS_DIR, ".nvmrc")):
        if os.path.isfile(nvmrc):
            with open(nvmrc) as f:
                spec = _nvm_resolve_alias(nvm_dir, f.read())
            if spec is not None:
                match = _nvm_match(installed, spec)
                if match:
                    candidates.append(match)
                else:
                    warn_(f"{nvmrc} demande Node {'.'.join(map(str, spec))} "
                          f"mais cette version n'est pas installée (nvm install)")
            break

    spec = _nvm_resolve_alias(nvm_dir, "default")
    if spec is not None:
        match = _nvm_match(installed, spec)
        if match:
            candidates.append(match)

    candidates.append(installed[0][1])
    return candidates


def _windows_candidates():
    dirs = []
    for var in ("NVM_SYMLINK",):
        if os.environ.get(var):
            dirs.append(os.environ[var])
    dirs.append(os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "nodejs"))
    return dirs


def _ensure_node_in_path():
    if shutil.which(_node_cmd()) and shutil.which(_npm_cmd()):
        return

    candidates = _windows_candidates() if IS_WINDOWS else _nvm_candidates()
    for d in candidates:
        if (os.path.isfile(os.path.join(d, _node_cmd()))
                and os.path.isfile(os.path.join(d, _npm_cmd()))):
            os.environ["PATH"] = d + os.pathsep + os.environ.get("PATH", "")
            info(f"Node.js trouvé via nvm (hors PATH) : {d}")
            return


def run():
    step("Building Tailwind CSS...")

    config_full_path = os.path.join(ASSETS_DIR, CONFIG_PATH)
    if not os.path.exists(config_full_path):
        fail_(f"{config_full_path} introuvable")

    output_full_path = os.path.join(ASSETS_DIR, OUTPUT_CSS)

    _ensure_node_in_path()

    if not shutil.which(_node_cmd()):
        fail_("Node.js introuvable - installe-le (ex: sudo pacman -S nodejs npm, ou via nvm)")

    if not shutil.which(_npm_cmd()):
        fail_("npm introuvable - installe-le (ex: sudo pacman -S npm)")

    node_version = subprocess.run([_node_cmd(), "--version"], capture_output=True,
                                  text=True, shell=IS_WINDOWS).stdout.strip()
    info(f"Node.js {node_version} ({shutil.which(_node_cmd())})")

    info("Installation des dépendances npm...")
    result = subprocess.run([_npm_cmd(), "install"], shell=IS_WINDOWS)
    if result.returncode != 0:
        fail_("npm install a échoué")

    input_full_path = os.path.join(ASSETS_DIR, INPUT_CSS)
    if not os.path.exists(input_full_path):
        info(f"{input_full_path} absent, création avec les directives Tailwind par défaut")
        os.makedirs(os.path.dirname(input_full_path), exist_ok=True)
        with open(input_full_path, "w") as f:
            f.write(DEFAULT_INPUT_CSS)

    cmd = [
        _npx_cmd(), "tailwindcss",
        "-c", CONFIG_PATH,
        "-i", INPUT_CSS,
        "-o", OUTPUT_CSS,
        "--minify",
    ]

    result = subprocess.run(cmd, cwd=ASSETS_DIR, shell=IS_WINDOWS)
    if result.returncode != 0:
        fail_("Tailwind build failed")

    if not os.path.exists(output_full_path):
        fail_(f"{output_full_path} n'a pas été généré")

    size = os.path.getsize(output_full_path)
    ok(f"Tailwind CSS généré ({size} octets) -> {output_full_path}")