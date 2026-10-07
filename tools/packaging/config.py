import os
import re

LAUNCHER_VERSION = "4.3.1"

ASSETS_ROOT = "assets"
DIST_ROOT   = "dist"

OUT_DAT      = "bin/assets/assets.dat"
OUT_CHECKSUM = "bin/assets/assets.dat.checksum"

MAX_FILE_SIZE    =  64 * 1024 * 1024
MAX_BUNDLE_SIZE  = 512 * 1024 * 1024
MAX_IMPORT_DEPTH  = 32
MAX_TOTAL_IMPORTS = 4096

ASSETS_ROOT_ABS = os.path.abspath(ASSETS_ROOT)

FORCE_INCLUDE_PREFIXES = (
    "agent/",
)

NOTE_INTERDITE = (
    "Ne jamais reactiver mangle.properties (regex ^_ ou autre) sans audit complet : "
    "le renommage de proprietes a deja ecrase window.t (i18n) par l'ex-_onGameStart "
    "-> bouton 'Fermer' a la place de 'Jouer' (incident 2026-09)."
)

ALLOWED_EXTENSIONS = {
    ".html", ".htm", ".css", ".js", ".json",
    ".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg", ".ico",
    ".woff", ".woff2", ".ttf", ".eot",
    ".mp3", ".ogg", ".wav", ".mp4", ".webm",
    ".jar",
}

RESERVED_NAMES = [
    "close_to_tray", "minimize_window", "quit_app",
    "launch_mc", "kill_game",
    "getMcToken", "getAccount", "getVersion", "setVersion",
    "loginMicrosoft", "checkAccount", "checkInternet",
    "onLaunchProgress", "onLaunchError",
    "_onGameStart", "_onGameStop",
    "t", "setLanguage", "applyTranslations",
    "save_settings", "get_settings",
    "updatePlayButton", "myUuid", "ceroWS",
    "SkinViewer", "WalkingAnimation",
    "VISUEL", "VERSIONS", "AMIS", "CHARGEMENT", "Jouer", "serveur",
]

VERSION_TAG_REGEX = re.compile(r'<%\s*version\s*%>', re.IGNORECASE)
IMPORT_REGEX      = re.compile(r'<%\s*import\s+[\'"]([^\'"]+)[\'"]\s*%>')
REF_REGEX         = re.compile(
    r'''(?:(?:src|href|data-src|data-href|poster|srcset|action|formaction|background)\s*=\s*['"]([^'"]+)['"])
      | (?:url\(\s*['"]?\s*([^'")\s]+)\s*['"]?\s*\))
      | (?:@import\s+['"]([^'"]+)['"])''',
    re.IGNORECASE | re.VERBOSE)
ASSET_HINT_REGEX = re.compile(r'(?://|<!--|/\*)\s*@asset\s+(\S+)')

JS_FUNC_DEF_RE = re.compile(
    r'''(?:
        function\s+([A-Za-z_$][\w$]*)
      | (?:const|let|var)\s+([A-Za-z_$][\w$]*)\s*=\s*(?:function\b|\([^)]*\)\s*=>)
      | (?:window|globalThis|self)\.([A-Za-z_$][\w$]*)\s*=
      | ([A-Za-z_$][\w$]*)\s*=\s*function\b
    )''', re.VERBOSE)
JS_CALL_RE      = re.compile(r'(?<![.\w$])([A-Za-z_$][\w$]*)\s*\(')
HTML_HANDLER_RE = re.compile(r'\bon[a-z]+\s*=\s*["\']([^"\']+)["\']', re.IGNORECASE)

JS_IMPORT_REGEX = re.compile(
    r'''(?:import\s+(?:[\w${},\s]+\s+from\s+)?['"]([^'"]+)['"])
      | (?:import\s*\(\s*['"]([^'"]+)['"]\s*\))
      | (?:export\s+(?:\*|\{[^}]*\})\s+from\s+['"]([^'"]+)['"])
      | (?:require\s*\(\s*['"]([^'"]+)['"]\s*\))
      | (?:new\s+Worker\s*\(\s*['"]([^'"]+)['"])''',
    re.VERBOSE)

JS_BUILTINS = {
    'if','for','while','switch','catch','return','typeof','new','delete','void',
    'function','async','await','do','throw','yield','in','of','instanceof',
    'console','window','document','globalThis','self','Math','JSON','Date',
    'Array','Object','String','Number','Boolean','Promise','Set','Map','RegExp',
    'parseInt','parseFloat','isNaN','isFinite','setTimeout','setInterval',
    'clearTimeout','clearInterval','fetch','alert','confirm','prompt',
    'requestAnimationFrame','cancelAnimationFrame','addEventListener',
    'removeEventListener','querySelector','querySelectorAll','getElementById',
    'createElement','Error','TypeError','RangeError','Symbol','BigInt',
}