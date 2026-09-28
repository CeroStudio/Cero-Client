#include "../include/platform/platform_defines.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#endif

#include "../include/app/app.h"
#include "../include/app/app_state.h"
#include "../include/platform/single_instance.h"
#include "../include/ui/ui.h"
#include "../include/net/ca_bundle.h"
#include "../include/launch/launch_minecraft.h"

#include <curl/curl.h>
#include <locale.h>

#ifdef _WIN32
  #include <windows.h>
  #include <stdio.h>

  static void cero_attach_console(void) {
      if (AttachConsole(ATTACH_PARENT_PROCESS)) {
          freopen("CONOUT$", "w", stdout);
          freopen("CONOUT$", "w", stderr);
          freopen("CONIN$", "r", stdin);
          setvbuf(stdout, NULL, _IONBF, 0);
          setvbuf(stderr, NULL, _IONBF, 0);
      }
  }
#endif

#ifdef __ANDROID__
int cero_main(int argc, char** argv) {

}
#else
int main(int argc, char** argv) {
#ifdef _WIN32
    cero_attach_console();
#endif
    setlocale(LC_NUMERIC, "C");

    curl_global_init(CURL_GLOBAL_DEFAULT);
    ca_bundle_ensure();

    LauncherOptions opts;
    if (!launcher_parse_args(argc, argv, &opts)) {
        curl_global_cleanup();
        return 1;
    }

    if (opts.launch_version) {
        if (!launcher_init(&opts)) {
            curl_global_cleanup();
            return 1;
        }
        launch_minecraft(opts.launch_version, NULL, NULL);
        curl_global_cleanup();
        return 0;
    }

    if (single_instance_check()) {
        curl_global_cleanup();
        return 0;
    }

    if (!launcher_init(&opts)) {
        curl_global_cleanup();
        return 1;
    }

    if (!launcher_create_ui())
        goto cleanup;

    launcher_bind_ui();
    launcher_start_services();
    ui_run(g_ui);

cleanup:
    launcher_shutdown();
    curl_global_cleanup();
    return 0;
}
#endif