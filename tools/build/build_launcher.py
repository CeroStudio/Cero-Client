import os
import sys
import subprocess
import urllib.request
import zipfile
import shutil
from pathlib import Path
from logger import step, ok, info, fail_

WEBVIEW2_NUGET_URL = "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2"
WEBVIEW2_CACHE_DIR = Path("third_party") / "webview2_sdk"

def ensure_webview2_sdk():
    include_dir = os.environ.get("WEBVIEW2_INCLUDE", "")
    lib_dir = os.environ.get("WEBVIEW2_LIB", "")
    if include_dir and lib_dir:

        return include_dir, lib_dir

    include_dir = str(WEBVIEW2_CACHE_DIR / "build" / "native" / "include")
    lib_dir = str(WEBVIEW2_CACHE_DIR / "build" / "native" / "x64")

    if os.path.exists(os.path.join(lib_dir, "WebView2Loader.dll.lib")):
        info("WebView2 SDK trouvé en cache (third_party/webview2_sdk/).")
        return include_dir, lib_dir

    info("WebView2 SDK absent localement, téléchargement (une seule fois)...")
    WEBVIEW2_CACHE_DIR.mkdir(parents=True, exist_ok=True)
    zip_path = WEBVIEW2_CACHE_DIR / "webview2.zip"

    try:
        urllib.request.urlretrieve(WEBVIEW2_NUGET_URL, zip_path)
        with zipfile.ZipFile(zip_path, "r") as archive:
            archive.extractall(WEBVIEW2_CACHE_DIR)
    except Exception as e:
        fail_(f"Échec du téléchargement/extraction du SDK WebView2 : {e}")
    finally:
        if zip_path.exists():
            zip_path.unlink()

    if not os.path.exists(os.path.join(lib_dir, "WebView2Loader.dll.lib")):
        fail_(f"WebView2Loader.dll.lib introuvable après extraction dans {lib_dir}")

    ok(f"SDK WebView2 prêt ({lib_dir})")
    return include_dir, lib_dir

def _platform_label():
    if sys.platform == "win32":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    return "linux/bsd"

def run():
    step(f"Building local launcher ({_platform_label()})")

    env = os.environ.copy()

    src_dir = Path("src")
    sources = sorted([str(p) for p in src_dir.rglob("*") if p.suffix in [".c", ".cpp", ".mm"]])

    if sys.platform != "darwin":
        sources = [s for s in sources if not s.endswith(".mm")]

    if sys.platform == "win32":
        sources = [s.replace("\\", "/") for s in sources]

    if not sources:
        fail_("No C/C++ sources found in src/")
    ok(f"{len(sources)} sources detected")

    objs = []
    for s in sources:
        obj = s.replace("src/", "obj/").replace(".cpp", ".o").replace(".c", ".o").replace(".mm", ".o")
        objs.append(obj)
    objs_str = " ".join(objs)

    if sys.platform == "win32":
        win_defs = "-D_WIN32 -DWIN32_LEAN_AND_MEAN -D_WINSOCKAPI_ -D_WIN32_WINNT=0x0601 -DNTDDI_VERSION=0x06010000"

        webview2_inc, webview2_lib = ensure_webview2_sdk()

        inc_flags = "-Iinclude -Ithird_party/webview/core/include"
        if webview2_inc:
            inc_flags += f" -I{webview2_inc}"

        lib_flags = ""
        if webview2_lib:
            lib_flags += f" -L{webview2_lib}"

        if os.path.exists("/usr/lib/libngtcp2_crypto_libressl.a") or os.path.exists("C:/msys64/mingw64/lib/libngtcp2_crypto_libressl.a"):
            curl_static_deps = "-Wl,--start-group -l:libcurl.a -l:libssh2.a -l:libnghttp2.a -l:libnghttp3.a -l:libngtcp2.a -l:libngtcp2_crypto_libressl.a -l:libssl.a -l:libcrypto.a -l:libz.a -l:libzstd.a -l:libbrotlidec.a -l:libbrotlicommon.a -l:libpsl.a -Wl,--end-group"
            curl_linked_statically = True
        else:
            curl_static_deps = "-lcurl -lssl -lcrypto -lssh2 -lnghttp2 -lnghttp3 -lz -lzstd -lbrotlidec -lbrotlicommon -lpsl -lws2_32 -lwldap32 -lcrypt32 -lnormaliz -lsecur32 -liphlpapi"
            curl_linked_statically = False

        win_libs = f"{lib_flags} -lws2_32 -lwldap32 -lcrypt32 -lnormaliz -lsecur32 -liphlpapi -l:WebView2Loader.dll.lib -lole32 -lshlwapi -lversion -ladvapi32 -luser32 -lshell32 -lgdi32 -static-libgcc -static-libstdc++ -ldwmapi -lwininet -lbcrypt -Wl,--defsym=fstat64=_fstat64 -s -Wl,-subsystem,windows"

        TAB = "\t"
        makefile_content = f"""CC       = gcc
CXX      = g++
CFLAGS   = -O2 -std=c11 -MMD -MP {inc_flags} {win_defs} -Wno-unused-function
CXXFLAGS = -O2 -std=c++17 -MMD -MP {inc_flags} {win_defs} -Wno-unused-function
LDFLAGS  = {curl_static_deps} {win_libs}

TARGET   = CeroClient.exe
OBJS     = {objs_str}

 $(TARGET): $(OBJS)
{TAB}$(CXX) -o $(TARGET) $(OBJS) $(LDFLAGS)

obj/%.o: src/%.c
{TAB}@mkdir -p $(dir $@)
{TAB}$(CC) $(CFLAGS) -c $< -o $@

obj/%.o: src/%.cpp
{TAB}@mkdir -p $(dir $@)
{TAB}$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)
"""
    elif sys.platform == "darwin":
        brew_prefix = "/opt/homebrew" if os.path.exists("/opt/homebrew") else "/usr/local"

        inc_flags = f"-Iinclude -Ithird_party/webview/core/include -I{brew_prefix}/include"
        lib_flags = f"-L{brew_prefix}/lib -lcurl"

        frameworks = "-framework WebKit -framework Cocoa -framework AppKit -framework Foundation"

        TAB = "\t"
        makefile_content = f"""CC       = clang
CXX      = clang++
CFLAGS   = -O2 -std=c11 -MMD -MP {inc_flags} -Wno-unused-function
CXXFLAGS = -O2 -std=c++17 -MMD -MP {inc_flags} -Wno-unused-function
LDFLAGS  = {lib_flags} {frameworks}

TARGET   = CeroClient
OBJS     = {objs_str}

 $(TARGET): $(OBJS)
{TAB}$(CXX) -o $(TARGET) $(OBJS) $(LDFLAGS)

obj/%.o: src/%.c
{TAB}@mkdir -p $(dir $@)
{TAB}$(CC) $(CFLAGS) -c $< -o $@

obj/%.o: src/%.cpp
{TAB}@mkdir -p $(dir $@)
{TAB}$(CXX) $(CXXFLAGS) -c $< -o $@

obj/%.o: src/%.mm
{TAB}@mkdir -p $(dir $@)
{TAB}$(CXX) $(CXXFLAGS) -x objective-c++ -c $< -o $@

-include $(OBJS:.o=.d)
"""
    else:
        pkg_config_path_export = ""
        if "freebsd" in sys.platform:
            bsd_paths = "/usr/local/lib/pkgconfig:/usr/local/libdata/pkgconfig"
            env["PKG_CONFIG_PATH"] = bsd_paths + ":" + env.get("PKG_CONFIG_PATH", "")
            pkg_config_path_export = f"export PKG_CONFIG_PATH={bsd_paths} && "

        webkit_pkg = "webkit2gtk-4.1"
        if subprocess.run(["pkg-config", "--exists", webkit_pkg], env=env).returncode != 0:
            webkit_pkg = "webkit2gtk-4.0"
            info("Using webkit2gtk-4.0")
        else:
            info("Using webkit2gtk-4.1")

        tray_cflags = ""
        tray_ldflags = ""
        tray_define = ""

        if subprocess.run(["pkg-config", "--exists", "ayatana-appindicator3-0.1"], env=env).returncode == 0:
            tray_cflags = subprocess.check_output(["pkg-config", "--cflags", "ayatana-appindicator3-0.1"], env=env).decode().strip()
            tray_ldflags = subprocess.check_output(["pkg-config", "--libs", "ayatana-appindicator3-0.1"], env=env).decode().strip()
            tray_define = "-DHAVE_AYATANA"
            info("Tray: Ayatana AppIndicator detected")
        elif subprocess.run(["pkg-config", "--exists", "appindicator3-0.1"], env=env).returncode == 0:
            tray_cflags = subprocess.check_output(["pkg-config", "--cflags", "appindicator3-0.1"], env=env).decode().strip()
            tray_ldflags = subprocess.check_output(["pkg-config", "--libs", "appindicator3-0.1"], env=env).decode().strip()
            tray_define = "-DHAVE_APPINDICATOR"
            info("Tray: AppIndicator detected")
        else:
            info("Tray: NOT detected - disabled")

        TAB = "\t"
        makefile_content = f"""CC       = cc
CXX      = c++
CFLAGS   = -O2 -MMD -MP -Iinclude -Ithird_party/webview/core/include {tray_define} {tray_cflags} $(shell {pkg_config_path_export}pkg-config --cflags libcurl)
CXXFLAGS = -O2 -std=c++17 -MMD -MP -Iinclude -Ithird_party/webview/core/include $(shell {pkg_config_path_export}pkg-config --cflags gtk+-3.0 {webkit_pkg} libcurl)
LDFLAGS  = $(shell {pkg_config_path_export}pkg-config --libs gtk+-3.0 {webkit_pkg} libcurl) {tray_ldflags}

TARGET   = CeroClient
OBJS     = {objs_str}

 $(TARGET): $(OBJS)
{TAB}$(CXX) -o $(TARGET) $(OBJS) $(LDFLAGS)

obj/%.o: src/%.c
{TAB}@mkdir -p $(dir $@)
{TAB}$(CC) $(CFLAGS) -c $< -o $@

obj/%.o: src/%.cpp
{TAB}@mkdir -p $(dir $@)
{TAB}$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)
"""

    with open("Makefile", "w") as f:
        f.write(makefile_content.strip() + "\n")
    ok("Makefile generated")

    info("Compiling launcher...")
    if sys.platform == "win32":
        make_cmd = "mingw32-make"
    elif "freebsd" in sys.platform:
        make_cmd = "gmake"
    else:
        make_cmd = "make"

    result = subprocess.run([make_cmd], env=env)

    if result.returncode != 0:
        fail_("Build failed")

    if os.path.exists("Makefile"):
        os.remove("Makefile")

    if sys.platform == "win32":
        bundle_windows_dlls(webview2_lib, curl_linked_statically)


def bundle_windows_dlls(webview2_lib, curl_linked_statically):
    step("Bundling required DLLs next to CeroClient.exe...")

    dest_dir = Path(".")
    copied = []

    webview2_dll = os.path.join(webview2_lib, "WebView2Loader.dll")
    if os.path.exists(webview2_dll):
        shutil.copy2(webview2_dll, dest_dir / "WebView2Loader.dll")
        copied.append("WebView2Loader.dll")
    else:
        fail_(f"WebView2Loader.dll introuvable dans {webview2_lib}")

    mingw_bin_candidates = [
        "C:/msys64/mingw64/bin",
        "/mingw64/bin",
    ]
    mingw_bin = next((p for p in mingw_bin_candidates if os.path.isdir(p)), None)
    if mingw_bin:
        winpthread_src = os.path.join(mingw_bin, "libwinpthread-1.dll")
        if os.path.exists(winpthread_src):
            shutil.copy2(winpthread_src, dest_dir / "libwinpthread-1.dll")
            copied.append("libwinpthread-1.dll")
        else:
            info("libwinpthread-1.dll introuvable dans le dossier bin MSYS2/mingw64.")
    else:
        info("Dossier bin MSYS2/mingw64 introuvable - libwinpthread-1.dll ne sera pas embarquée "
             "(l'exe ne démarrera pas sans elle).")

    if not curl_linked_statically:
        mingw_bin_candidates = [
            "C:/msys64/mingw64/bin",
            "/mingw64/bin",
        ]
        mingw_bin = next((p for p in mingw_bin_candidates if os.path.isdir(p)), None)

        if not mingw_bin:
            info("libcurl est lié dynamiquement mais le dossier bin MSYS2/mingw64 est introuvable - "
                 "les DLLs runtime (libcurl, libssl, ...) ne seront pas embarquées.")
        else:
            runtime_dlls = [
                "libcurl-4.dll", "libssl-3-x64.dll", "libcrypto-3-x64.dll",
                "libssh2-1.dll", "libnghttp2-14.dll", "libnghttp3-9.dll",
                "libngtcp2-16.dll", "libngtcp2_crypto_libressl-0.dll",
                "zlib1.dll", "libzstd-1.dll", "libbrotlidec.dll", "libbrotlicommon.dll",
                "libpsl-5.dll", "libidn2-0.dll", "libintl-8.dll", "libiconv-2.dll",
                "libunistring-5.dll","libngtcp2_crypto_ossl-0.dll", "libzstd.dll",
            ]
            for name in runtime_dlls:
                src = os.path.join(mingw_bin, name)
                if os.path.exists(src):
                    shutil.copy2(src, dest_dir / name)
                    copied.append(name)

    ok(f"{len(copied)} DLL(s) copiée(s) à côté de CeroClient.exe: {', '.join(copied)}")