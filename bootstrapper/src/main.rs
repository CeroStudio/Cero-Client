#![cfg_attr(all(not(debug_assertions), target_os = "windows"), windows_subsystem = "windows")]

mod archive;
mod deps;
mod http;
mod log;
mod platform;
mod update;

use log::Log;

fn main() {
    platform::cleanup_old_self();

    let log = Log::new();
    let agent = http::agent();

    match update::self_update(&agent, &log) {
        Ok(true) => std::process::exit(0),
        Ok(false) => {}
        Err(e) => log.warn(&format!("Self-update échoué : {e} (on continue)")),
    }

    log.banner();

    let bin = platform::bin_dir();

    log.section("Mise à jour du launcher et des assets");
    if let Err(e) = update::ensure_zip_asset(&agent, &platform::client_zip_name(), &bin, &bin, &log)
    {
        log.err(&format!("Launcher/assets : {e}"));
        std::process::exit(1);
    }
    platform::make_executable(&bin.join(platform::LAUNCHER_NAME));

    #[cfg(windows)]
    {
        log.section("Mise à jour des DLLs");
        if let Err(e) =
            update::ensure_zip_asset(&agent, "CeroClient_windows_dll.zip", &bin, &bin, &log)
        {
            log.err(&format!("DLLs : {e}"));
            std::process::exit(1);
        }
    }

    let launcher_path = bin.join(platform::LAUNCHER_NAME);
    if let Err(e) = deps::check(&launcher_path, &log) {
        log.err(&e);
        std::process::exit(2);
    }

    log.section("Lancement");
    log.info(&format!("{}", launcher_path.display()));

    if let Err(e) = platform::spawn_detached(&launcher_path, &bin) {
        log.err(&e);
        std::process::exit(1);
    }

    log.done("Launcher démarré !");
    std::process::exit(0);
}
