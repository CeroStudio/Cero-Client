use serde::{Deserialize, Serialize};
use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::mpsc::{channel, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

const DEFAULT_IMAGE: &str = "ghcr.io/cerostudio/ceroclient-server";
const DEFAULT_TAG: &str = "main";
const DEFAULT_NAME: &str = "ceroclient-server";
const DEFAULT_PORT: u16 = 3134;
const DEFAULT_DATA_VOLUME: &str = "ceroclient-data";
const DEFAULT_DATA_MOUNT: &str = "/data";

const FAST_CRASH_WINDOW: Duration = Duration::from_secs(15);
const FAST_CRASH_LIMIT: u32 = 3;
const BACKOFF_MIN: Duration = Duration::from_secs(1);
const BACKOFF_MAX: Duration = Duration::from_secs(60);

#[derive(Default, Serialize, Deserialize)]
struct State {
    #[serde(default, skip_serializing_if = "String::is_empty")]
    installed: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    previous: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    bad: String,
}

enum Event {
    Exit {
        gen: u64,
        success: bool,
        code: Option<i32>,
    },
    Shutdown,
    Ready(Option<String>),
}

#[derive(Clone)]
enum DataVol {
    Bind(PathBuf),
    Named(String),
}

struct Config {
    docker_bin: String,
    image: String,
    tag: String,
    name: String,
    docker_args: Vec<String>,
    port: u16,
    interval: Duration,
    stop_timeout: Duration,
    state_path: PathBuf,
    registry_user: String,
    registry_pass: String,
    assume_current: bool,
    data: Option<DataVol>,
    data_mount: String,
    args: Vec<String>,
}

struct ServerProc {
    attach: Child,
    started: Instant,
    digest: String,
}

fn main() {
    let cfg = match parse_args() {
        Ok(c) => c,
        Err(e) => {
            eprintln!("[ceroboot] {}", e);
            usage();
            std::process::exit(2);
        }
    };
    if let Err(e) = run(cfg) {
        eprintln!("[ceroboot] fatal : {}", e);
        std::process::exit(1);
    }
}

fn run(cfg: Config) -> Result<(), String> {
    let cfg = Arc::new(cfg);
    docker_login(&cfg)?;

    match &cfg.data {
        Some(DataVol::Bind(dir)) => log(&format!(
            "données sauvegardées dans {} (montées sur {})",
            dir.display(),
            cfg.data_mount
        )),
        Some(DataVol::Named(vol)) => log(&format!(
            "données sauvegardées dans le volume docker \"{}\" (monté sur {})",
            vol, cfg.data_mount
        )),
        None => log(
            "persistance des données désactivée : tout ce qui est écrit dans le \
             conteneur sera perdu à la prochaine mise à jour",
        ),
    }

    let (tx, rx) = channel::<Event>();
    {
        let tx = tx.clone();
        ctrlc::set_handler(move || {
            let _ = tx.send(Event::Shutdown);
        })
        .map_err(|e| e.to_string())?;
    }

    let st = Arc::new(Mutex::new(load_state(&cfg.state_path)));
    let stdin_target: Arc<Mutex<Option<ChildStdin>>> = Arc::new(Mutex::new(None));
    spawn_stdin_pump(Arc::clone(&stdin_target));

    let mut cur: Option<ServerProc> = None;
    let mut gen: u64 = 0;

    bootstrap(&cfg, &tx, &st, &stdin_target, &mut cur, &mut gen)?;

    let mut downloading = true;
    let mut dl_handle: Option<JoinHandle<()>> = Some(spawn_check(&cfg, &st, tx.clone()));

    let mut next_check = Instant::now() + cfg.interval;
    let mut fast_crashes: u32 = 0;
    let mut backoff = BACKOFF_MIN;
    let mut restart_at: Option<Instant> = None;

    loop {
        let now = Instant::now();
        let mut timeout = next_check.saturating_duration_since(now);
        if let Some(ra) = restart_at {
            timeout = timeout.min(ra.saturating_duration_since(now));
        }
        match rx.recv_timeout(timeout) {
            Ok(Event::Shutdown) => {
                log("arrêt demandé — arrêt du conteneur");
                stop_server(&cfg, &stdin_target, &mut cur, &mut gen);
                return Ok(());
            }
            Ok(Event::Exit { gen: g, success, code }) => {
                if g != gen {
                    continue;
                }
                let lived = cur
                    .as_ref()
                    .map(|p| p.started.elapsed())
                    .unwrap_or_default();
                let digest = cur
                    .as_ref()
                    .map(|p| p.digest.clone())
                    .unwrap_or_else(|| "inconnu".to_string());
                cur = None;
                {
                    let mut slot = lock(&stdin_target);
                    *slot = None;
                }
                if success {
                    log(&format!(
                        "le conteneur ({}) s'est arrêté proprement après {} — bye",
                        digest,
                        fmt_dur(lived)
                    ));
                    return Ok(());
                }
                let cause = match code {
                    Some(c) => format!("exit status {}", c),
                    None => "état de sortie illisible (docker wait a échoué)".to_string(),
                };
                log(&format!(
                    "le conteneur ({}) a quitté après {} : {}",
                    digest,
                    fmt_dur(lived),
                    cause
                ));
                if downloading {
                    log("mise à jour en cours — j'attends le nouveau digest");
                    continue;
                }
                if lived < FAST_CRASH_WINDOW {
                    fast_crashes += 1;
                } else {
                    fast_crashes = 0;
                    backoff = BACKOFF_MIN;
                }
                if fast_crashes >= FAST_CRASH_LIMIT {
                    fast_crashes = 0;
                    if let Some(digest) = rollback(&cfg, &st) {
                        start(&cfg, &tx, &stdin_target, &mut cur, &mut gen, &digest, false)?;
                        continue;
                    }
                }
                restart_at = Some(Instant::now() + backoff);
                log(&format!("redémarrage dans {}", fmt_dur(backoff)));
                backoff = std::cmp::min(backoff * 2, BACKOFF_MAX);
            }
            Ok(Event::Ready(candidate)) => {
                downloading = false;
                dl_handle = None;
                next_check = Instant::now() + cfg.interval;
                match candidate {
                    None => {
                        if cur.is_none() {
                            restart_at = None;
                            let digest = lock(&st).installed.clone();
                            start(&cfg, &tx, &stdin_target, &mut cur, &mut gen, &digest, false)?;
                        }
                    }
                    Some(digest) => {
                        log("nouvelle version prête — arrêt du conteneur");
                        stop_server(&cfg, &stdin_target, &mut cur, &mut gen);
                        {
                            let mut s = lock(&st);
                            s.previous = s.installed.clone();
                            s.installed = digest.clone();
                            if let Err(e) = save_state(&cfg.state_path, &s) {
                                log(&format!("état non sauvegardé : {}", e));
                            }
                        }
                        log(&format!("digest {} installé — redémarrage", digest));
                        start(&cfg, &tx, &stdin_target, &mut cur, &mut gen, &digest, false)?;
                        fast_crashes = 0;
                        backoff = BACKOFF_MIN;
                        restart_at = None;
                    }
                }
            }
            Err(RecvTimeoutError::Timeout) => {
                let now = Instant::now();
                if downloading {
                    if let Some(h) = &dl_handle {
                        if h.is_finished() {
                            downloading = false;
                            dl_handle = None;
                        }
                    }
                    if downloading {
                        next_check = now + cfg.interval;
                        continue;
                    }
                }
                if let Some(ra) = restart_at {
                    if ra <= now {
                        restart_at = None;
                        if cur.is_none() {
                            let digest = lock(&st).installed.clone();
                            start(&cfg, &tx, &stdin_target, &mut cur, &mut gen, &digest, false)?;
                        }
                        continue;
                    }
                }
                if !downloading && next_check <= now {
                    next_check = now + cfg.interval;
                    downloading = true;
                    dl_handle = Some(spawn_check(&cfg, &st, tx.clone()));
                }
            }
            Err(RecvTimeoutError::Disconnected) => {
                return Err("canal interne fermé".to_string());
            }
        }
    }
}

fn bootstrap(
    cfg: &Config,
    tx: &Sender<Event>,
    st: &Arc<Mutex<State>>,
    stdin_target: &Arc<Mutex<Option<ChildStdin>>>,
    cur: &mut Option<ServerProc>,
    gen: &mut u64,
) -> Result<(), String> {
    match container_running(cfg) {
        Some(true) if cfg.assume_current => {
            let installed_empty = lock(st).installed.is_empty();
            if installed_empty {
                if let Some(d) = container_image_digest(cfg) {
                    let mut s = lock(st);
                    s.installed = d.clone();
                    if let Err(e) = save_state(&cfg.state_path, &s) {
                        log(&format!("état non sauvegardé : {}", e));
                    }
                    log(&format!("conteneur existant adopté comme {}", d));
                } else {
                    log("conteneur existant, mais son digest est illisible (pas tiré d'un registre ?)");
                }
            }
            let digest = lock(st).installed.clone();
            start(cfg, tx, stdin_target, cur, gen, &digest, true)
        }
        _ => {
            remove_container(cfg);
            log(&format!(
                "installation depuis {}:{}",
                cfg.image, cfg.tag
            ));
            pull_tag(cfg)?;
            let tag_ref = format!("{}:{}", cfg.image, cfg.tag);
            let digest = repo_digest(cfg, &tag_ref)?;
            {
                let mut s = lock(st);
                s.previous = s.installed.clone();
                s.installed = digest.clone();
                if let Err(e) = save_state(&cfg.state_path, &s) {
                    log(&format!("état non sauvegardé : {}", e));
                }
            }
            start(cfg, tx, stdin_target, cur, gen, &digest, false)
        }
    }
}

fn start(
    cfg: &Config,
    tx: &Sender<Event>,
    stdin_target: &Arc<Mutex<Option<ChildStdin>>>,
    cur: &mut Option<ServerProc>,
    gen: &mut u64,
    digest_ref: &str,
    adopt_existing: bool,
) -> Result<(), String> {
    if digest_ref.is_empty() {
        return Err("aucun digest installé à lancer".to_string());
    }

    if !adopt_existing {
        remove_container(cfg);
        ensure_image(cfg, digest_ref)?;

        let mut run_cmd = docker(cfg);
        run_cmd
            .arg("run")
            .arg("-d")
            .arg("--name")
            .arg(&cfg.name)
            .arg("-i");

        match &cfg.data {
            Some(DataVol::Bind(dir)) => {
                if let Err(e) = fs::create_dir_all(dir) {
                    return Err(format!(
                        "création du répertoire de données {} impossible : {}",
                        dir.display(),
                        e
                    ));
                }
                run_cmd
                    .arg("-v")
                    .arg(format!("{}:{}", dir.display(), cfg.data_mount));
            }
            Some(DataVol::Named(vol)) => {
                run_cmd
                    .arg("-v")
                    .arg(format!("{}:{}", vol, cfg.data_mount));
            }
            None => {}
        }

        if cfg.port != 0 {
            run_cmd
                .arg("-p")
                .arg(format!("{}:{}/tcp", cfg.port, cfg.port));
        }
        for extra in &cfg.docker_args {
            run_cmd.arg(extra);
        }
        run_cmd.arg(digest_ref);
        for a in &cfg.args {
            run_cmd.arg(a);
        }
        let status = run_cmd
            .stdout(Stdio::null())
            .status()
            .map_err(|e| format!("docker run : {}", e))?;
        if !status.success() {
            return Err(format!("docker run {} a échoué", digest_ref));
        }
    }

    *gen += 1;
    let my_gen = *gen;

    {
        let docker_bin = cfg.docker_bin.clone();
        let name = cfg.name.clone();
        let watcher_tx = tx.clone();
        thread::spawn(move || {
            let out = Command::new(&docker_bin).args(["wait", &name]).output();
            let (success, code) = match out {
                Ok(o) if o.status.success() => {
                    let text = String::from_utf8_lossy(&o.stdout);
                    match text.trim().parse::<i64>() {
                        Ok(0) => (true, Some(0)),
                        Ok(c) => (false, Some(c as i32)),
                        Err(_) => (false, None),
                    }
                }
                _ => (false, None),
            };
            let _ = watcher_tx.send(Event::Exit {
                gen: my_gen,
                success,
                code,
            });
        });
    }

    let mut attach = docker(cfg)
        .args(["attach", "--sig-proxy=false", &cfg.name])
        .stdin(Stdio::piped())
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .spawn()
        .map_err(|e| format!("docker attach : {}", e))?;
    let attach_stdin = attach.stdin.take();
    {
        let mut slot = lock(stdin_target);
        *slot = attach_stdin;
    }

    log(&format!(
        "conteneur {} {} (digest {})",
        cfg.name,
        if adopt_existing { "rattaché" } else { "lancé" },
        digest_ref
    ));

    *cur = Some(ServerProc {
        attach,
        started: Instant::now(),
        digest: digest_ref.to_string(),
    });
    Ok(())
}

fn stop_server(
    cfg: &Config,
    stdin_target: &Arc<Mutex<Option<ChildStdin>>>,
    cur: &mut Option<ServerProc>,
    gen: &mut u64,
) {
    let mut p = match cur.take() {
        Some(p) => p,
        None => return,
    };
    *gen += 1;
    {
        let mut slot = lock(stdin_target);
        *slot = None;
    }
    log(&format!(
        "arrêt du conteneur {} (gracieux, {} max — docker stop gère lui-même le SIGKILL de secours)",
        cfg.name,
        fmt_dur(cfg.stop_timeout)
    ));
    let secs = cfg.stop_timeout.as_secs().max(1).to_string();
    let stopped = docker(cfg)
        .args(["stop", "-t", &secs, &cfg.name])
        .stdout(Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false);
    if !stopped {
        log("docker stop a échoué — docker kill");
        let _ = docker(cfg)
            .args(["kill", &cfg.name])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
    }
    let _ = p.attach.kill();
    let _ = p.attach.wait();
}

fn rollback(cfg: &Config, st: &Arc<Mutex<State>>) -> Option<String> {
    let previous = lock(st).previous.clone();
    if previous.is_empty() {
        log(&format!(
            "{} crashs rapides — aucune version précédente, je continue en backoff",
            FAST_CRASH_LIMIT
        ));
        return None;
    }
    if let Err(e) = ensure_image(cfg, &previous) {
        log(&format!("rollback impossible ({}) — je continue en backoff", e));
        return None;
    }
    let broken = lock(st).installed.clone();
    {
        let mut s = lock(st);
        s.bad = broken.clone();
        s.installed = previous.clone();
        s.previous = String::new();
        if let Err(e) = save_state(&cfg.state_path, &s) {
            log(&format!("état non sauvegardé : {}", e));
        }
    }
    log(&format!(
        "{} crashs rapides — retour à {}, {} est marqué mauvais",
        FAST_CRASH_LIMIT,
        previous,
        or_unknown(&broken)
    ));
    Some(previous)
}

fn spawn_check(cfg: &Arc<Config>, st: &Arc<Mutex<State>>, tx: Sender<Event>) -> JoinHandle<()> {
    let cfg = Arc::clone(cfg);
    let st = Arc::clone(st);
    thread::spawn(move || {
        let candidate = check_for_update(&cfg, &st);
        let _ = tx.send(Event::Ready(candidate));
    })
}

fn check_for_update(cfg: &Config, st: &Arc<Mutex<State>>) -> Option<String> {
    if let Err(e) = pull_tag(cfg) {
        log(&format!("vérification échouée : {}", e));
        return None;
    }
    let tag_ref = format!("{}:{}", cfg.image, cfg.tag);
    let digest = match repo_digest(cfg, &tag_ref) {
        Ok(d) => d,
        Err(e) => {
            log(&format!("vérification échouée : {}", e));
            return None;
        }
    };
    let installed = lock(st).installed.clone();
    let bad = lock(st).bad.clone();
    if digest == installed || digest == bad {
        return None;
    }
    log(&format!(
        "nouvelle version disponible : {} (installée : {})",
        digest,
        or_unknown(&installed)
    ));
    Some(digest)
}

fn docker(cfg: &Config) -> Command {
    Command::new(&cfg.docker_bin)
}

fn docker_login(cfg: &Config) -> Result<(), String> {
    if cfg.registry_user.is_empty() {
        return Ok(());
    }
    let mut cmd = docker(cfg);
    cmd.arg("login");
    if let Some(host) = registry_host(&cfg.image) {
        cmd.arg(host);
    }
    cmd.args(["-u", &cfg.registry_user, "--password-stdin"]);
    cmd.stdin(Stdio::piped()).stdout(Stdio::null());
    let mut child = cmd.spawn().map_err(|e| format!("docker login : {}", e))?;
    if let Some(mut s) = child.stdin.take() {
        s.write_all(cfg.registry_pass.as_bytes())
            .map_err(|e| format!("docker login : {}", e))?;
    }
    let status = child.wait().map_err(|e| format!("docker login : {}", e))?;
    if !status.success() {
        return Err("docker login a échoué (identifiants du registre invalides ?)".to_string());
    }
    Ok(())
}

fn registry_host(image: &str) -> Option<String> {
    let first = image.split('/').next().unwrap_or("");
    if first.contains('.') || first.contains(':') || first == "localhost" {
        Some(first.to_string())
    } else {
        None
    }
}

fn pull_tag(cfg: &Config) -> Result<(), String> {
    let reference = format!("{}:{}", cfg.image, cfg.tag);
    let status = docker(cfg)
        .args(["pull", &reference])
        .status()
        .map_err(|e| format!("docker pull {} : {}", reference, e))?;
    if !status.success() {
        return Err(format!("docker pull {} a échoué", reference));
    }
    Ok(())
}

fn repo_digest(cfg: &Config, reference: &str) -> Result<String, String> {
    let out = docker(cfg)
        .args([
            "image",
            "inspect",
            "--format",
            "{{index .RepoDigests 0}}",
            reference,
        ])
        .output()
        .map_err(|e| format!("docker image inspect {} : {}", reference, e))?;
    if !out.status.success() {
        return Err(format!(
            "docker image inspect {} a échoué : {}",
            reference,
            String::from_utf8_lossy(&out.stderr).trim()
        ));
    }
    let digest = String::from_utf8_lossy(&out.stdout).trim().to_string();
    if digest.is_empty() || digest == "<no value>" {
        return Err(format!(
            "{} n'a pas de RepoDigest (image construite localement, jamais poussée sur un registre ?)",
            reference
        ));
    }
    Ok(digest)
}

fn image_present(cfg: &Config, reference: &str) -> bool {
    docker(cfg)
        .args(["image", "inspect", reference])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

fn ensure_image(cfg: &Config, digest_ref: &str) -> Result<(), String> {
    if image_present(cfg, digest_ref) {
        return Ok(());
    }
    let status = docker(cfg)
        .args(["pull", digest_ref])
        .status()
        .map_err(|e| format!("docker pull {} : {}", digest_ref, e))?;
    if !status.success() {
        return Err(format!("docker pull {} a échoué", digest_ref));
    }
    Ok(())
}

fn container_running(cfg: &Config) -> Option<bool> {
    let out = docker(cfg)
        .args(["inspect", "--format", "{{.State.Running}}", &cfg.name])
        .output()
        .ok()?;
    if !out.status.success() {
        return None;
    }
    let running = String::from_utf8_lossy(&out.stdout).trim() == "true";
    Some(running)
}

fn container_image_digest(cfg: &Config) -> Option<String> {
    let out = docker(cfg)
        .args(["inspect", "--format", "{{.Image}}", &cfg.name])
        .output()
        .ok()?;
    if !out.status.success() {
        return None;
    }
    let image_id = String::from_utf8_lossy(&out.stdout).trim().to_string();
    if image_id.is_empty() {
        return None;
    }
    repo_digest(cfg, &image_id).ok()
}

fn remove_container(cfg: &Config) {
    let _ = docker(cfg)
        .args(["rm", "-f", &cfg.name])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
}

fn spawn_stdin_pump(target: Arc<Mutex<Option<ChildStdin>>>) {
    thread::spawn(move || {
        let mut input = std::io::stdin();
        let mut buf = [0u8; 8192];
        loop {
            match input.read(&mut buf) {
                Ok(0) => break,
                Ok(n) => {
                    let mut guard = lock(&target);
                    if let Some(w) = guard.as_mut() {
                        if w.write_all(&buf[..n]).is_err() {
                            *guard = None;
                        }
                    }
                }
                Err(_) => break,
            }
        }
    });
}

fn load_state(path: &Path) -> State {
    match fs::read_to_string(path) {
        Ok(data) => match serde_json::from_str(&data) {
            Ok(st) => st,
            Err(e) => {
                log(&format!("état illisible ({}) — on repart de zéro", e));
                State::default()
            }
        },
        Err(_) => State::default(),
    }
}

fn save_state(path: &Path, st: &State) -> Result<(), String> {
    let data = serde_json::to_vec_pretty(st).map_err(|e| e.to_string())?;
    let tmp = suffixed(path, ".tmp");
    fs::write(&tmp, data).map_err(|e| e.to_string())?;
    fs::rename(&tmp, path).map_err(|e| e.to_string())
}

fn parse_args() -> Result<Config, String> {
    let argv: Vec<String> = std::env::args().skip(1).collect();
    let mut docker_bin = "docker".to_string();
    let mut image = DEFAULT_IMAGE.to_string();
    let mut tag = DEFAULT_TAG.to_string();
    let mut name = DEFAULT_NAME.to_string();
    let mut docker_args: Vec<String> = Vec::new();
    let mut port: u16 = DEFAULT_PORT;
    let mut interval = Duration::from_secs(30);
    let mut stop_timeout = Duration::from_secs(30);
    let mut state_path = ".ceroboot.json".to_string();
    let mut registry_user = String::new();
    let mut registry_pass = String::new();
    let mut assume_current = false;
    let mut data: Option<String> = Some(DEFAULT_DATA_VOLUME.to_string());
    let mut data_mount = DEFAULT_DATA_MOUNT.to_string();
    let mut server_args: Vec<String> = Vec::new();
    let mut i = 0usize;
    while i < argv.len() {
        let arg = argv[i].clone();
        if arg == "--" {
            server_args = argv[i + 1..].to_vec();
            break;
        }
        if arg == "-h" || arg == "-help" || arg == "--help" {
            usage();
            std::process::exit(0);
        }
        let (raw_name, inline) = split_eq(&arg);
        let name_opt = raw_name.trim_start_matches('-').to_string();
        let is_flag = name_opt == "assume-current" || name_opt == "no-port" || name_opt == "no-data";
        let value = if let Some(v) = inline {
            Some(v)
        } else if is_flag {
            if i + 1 < argv.len() && (argv[i + 1] == "true" || argv[i + 1] == "false") {
                i += 1;
                Some(argv[i].clone())
            } else {
                None
            }
        } else {
            i += 1;
            if i >= argv.len() {
                return Err(format!("valeur manquante pour {}", arg));
            }
            Some(argv[i].clone())
        };
        let v = value.unwrap_or_else(|| "true".to_string());
        match name_opt.as_str() {
            "docker" => docker_bin = v,
            "image" => image = v,
            "tag" => tag = v,
            "name" => name = v,
            "docker-arg" => docker_args.push(v),
            "port" => port = v.parse().map_err(|_| format!("port invalide : {}", v))?,
            "no-port" => {
                if v == "true" || v == "1" {
                    port = 0;
                }
            }
            "interval" => {
                interval = parse_duration(&v).ok_or_else(|| format!("durée invalide : {}", v))?
            }
            "stop-timeout" => {
                stop_timeout =
                    parse_duration(&v).ok_or_else(|| format!("durée invalide : {}", v))?
            }
            "state" => state_path = v,
            "registry-user" => registry_user = v,
            "registry-pass" => registry_pass = v,
            "assume-current" => assume_current = v == "true" || v == "1",
            "data" => {
                data = if v.is_empty() { None } else { Some(v) };
            }
            "data-mount" => data_mount = v,
            "no-data" => {
                if v == "true" || v == "1" {
                    data = None;
                }
            }
            other => return Err(format!("option inconnue : {}", other)),
        }
        i += 1;
    }
    if registry_pass.is_empty() {
        registry_pass = std::env::var("CEROBOOT_REGISTRY_PASSWORD").unwrap_or_default();
    }
    if data_mount.trim().is_empty() {
        data_mount = DEFAULT_DATA_MOUNT.to_string();
    }
    let data = data.map(|v| {
        if v.contains('/') || v.contains('\\') || v == "." || v == ".." {
            DataVol::Bind(abs_path(&v))
        } else {
            DataVol::Named(v)
        }
    });
    Ok(Config {
        docker_bin,
        image,
        tag,
        name,
        docker_args,
        port,
        interval,
        stop_timeout,
        state_path: abs_path(&state_path),
        registry_user,
        registry_pass,
        assume_current,
        data,
        data_mount,
        args: server_args,
    })
}

fn usage() {
    eprintln!("ceroboot — supervise le conteneur Cero et applique les mises à jour en place");
    eprintln!();
    eprintln!("Usage: ceroboot [options] [-- args du serveur]");
    eprintln!();
    eprintln!("  -docker BIN           binaire docker à utiliser (défaut : docker)");
    eprintln!("  -image IMAGE          image à superviser (défaut : {})", DEFAULT_IMAGE);
    eprintln!("  -tag TAG              tag suivi pour les mises à jour (défaut : {})", DEFAULT_TAG);
    eprintln!("  -name NOM             nom du conteneur (défaut : {})", DEFAULT_NAME);
    eprintln!("  -docker-arg ARG       argument supplémentaire pour `docker run` (répétable,");
    eprintln!("                        ex. -docker-arg=-v -docker-arg=./data:/data)");
    eprintln!("  -port PORT            port publié en <PORT>:<PORT>/tcp (défaut : {})", DEFAULT_PORT);
    eprintln!("  -no-port              ne publie aucun port par défaut (utiliser -docker-arg -p à la place)");
    eprintln!("  -data CIBLE           sauvegarde des données du conteneur, montées sur -data-mount :");
    eprintln!("                        nom simple => volume Docker nommé (défaut : {})", DEFAULT_DATA_VOLUME);
    eprintln!("                        chemin     => dossier de l'hôte (ex. -data ./monde)");
    eprintln!("                        -data= (vide) ou -no-data pour désactiver");
    eprintln!("  -data-mount CHEMIN    point de montage dans le conteneur (défaut : {})", DEFAULT_DATA_MOUNT);
    eprintln!("  -interval DUREE       intervalle entre deux vérifications (défaut : 30s)");
    eprintln!("  -stop-timeout DUREE   délai accordé à `docker stop` (défaut : 30s)");
    eprintln!("  -state FICHIER        fichier d'état (défaut : .ceroboot.json)");
    eprintln!("  -registry-user USER   utilisateur pour `docker login` sur le registre de -image");
    eprintln!("  -registry-pass MDP    mot de passe/token (défaut : CEROBOOT_REGISTRY_PASSWORD)");
    eprintln!("  -assume-current       si un conteneur -name tourne déjà, l'adopter tel quel");
    eprintln!();
    eprintln!("Le conteneur est lancé avec `-i` (stdin gardé ouvert) ; ceroboot s'y attache");
    eprintln!("avec `docker attach` pour relayer stdin et suivre stdout/stderr en direct.");
}

fn parse_duration(s: &str) -> Option<Duration> {
    let s = s.trim();
    if s.is_empty() {
        return None;
    }
    let (num, mult) = match s.as_bytes()[s.len() - 1] {
        b's' => (&s[..s.len() - 1], 1u64),
        b'm' => (&s[..s.len() - 1], 60),
        b'h' => (&s[..s.len() - 1], 3600),
        _ => (s, 1),
    };
    let n: f64 = num.parse().ok()?;
    if n < 0.0 {
        return None;
    }
    Some(Duration::from_millis((n * mult as f64 * 1000.0) as u64))
}

fn split_eq(arg: &str) -> (String, Option<String>) {
    match arg.find('=') {
        Some(pos) => (arg[..pos].to_string(), Some(arg[pos + 1..].to_string())),
        None => (arg.to_string(), None),
    }
}

fn abs_path(p: &str) -> PathBuf {
    let path = PathBuf::from(p);
    if path.is_absolute() {
        path
    } else {
        match std::env::current_dir() {
            Ok(cwd) => cwd.join(path),
            Err(_) => path,
        }
    }
}

fn suffixed(p: &Path, suffix: &str) -> PathBuf {
    let mut os = p.as_os_str().to_os_string();
    os.push(suffix);
    PathBuf::from(os)
}

fn log(msg: &str) {
    eprintln!("[ceroboot] {}", msg);
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    match m.lock() {
        Ok(g) => g,
        Err(p) => p.into_inner(),
    }
}

fn or_unknown(v: &str) -> String {
    if v.is_empty() {
        "inconnue".to_string()
    } else {
        v.to_string()
    }
}

fn fmt_dur(d: Duration) -> String {
    let s = d.as_secs_f64();
    if s >= 1.0 {
        format!("{}s", s)
    } else {
        format!("{}ms", d.as_millis())
    }
}