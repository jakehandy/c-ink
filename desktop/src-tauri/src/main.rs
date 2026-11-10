// C-Ink Settings: the small Mac / Windows app. It hosts the bridge (Claude
// Code runs, LAN endpoint, relay upload), sets up TICKEYs over USB, and shows
// a pixel-exact preview of what the device will draw.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use base64::Engine;
use cink_bridge::{claude, device, digest, kinds::KINDS, preview, prompt, tz, Bridge, Settings};
use serde::Deserialize;
use serde_json::{json, Value};
use tauri::menu::{Menu, MenuItem};
use tauri::tray::TrayIconBuilder;
use tauri::{Manager, State, WindowEvent};
use tauri_plugin_autostart::ManagerExt;

struct App {
    bridge: Bridge,
}

type Res<T> = Result<T, String>;

fn err(e: impl std::fmt::Display) -> String {
    e.to_string()
}

async fn blocking<T: Send + 'static>(f: impl FnOnce() -> anyhow::Result<T> + Send + 'static) -> Res<T> {
    tauri::async_runtime::spawn_blocking(f).await.map_err(err)?.map_err(|e| format!("{e:#}"))
}

#[tauri::command]
fn get_state(app: State<App>) -> Value {
    let s = app.bridge.settings.read().clone();
    let connectors = prompt::connector_names(&s.tools);
    let screens: Vec<Value> = KINDS
        .iter()
        .map(|k| {
            let found: Vec<&String> =
                connectors.iter().filter(|c| k.connectors.iter().any(|n| c.to_lowercase().contains(n))).collect();
            let gh_fallback = k.key == "review" && found.is_empty() && which_gh();
            json!({
                "key": k.key, "name": k.name, "blurb": k.blurb, "enabled": s.enabled(k.key),
                "needs": k.connectors, "connected": found, "derived": k.connectors.is_empty(),
                "gh_fallback": gh_fallback,
            })
        })
        .collect();
    json!({
        "settings": s,
        "screens": screens,
        "connectors": connectors,
        "status": app.bridge.status(),
        "timezone": tz::iana(),
        "host": cink_bridge::service::host_name(),
    })
}

fn which_gh() -> bool {
    claude::gh_context_available()
}

#[tauri::command]
fn save_settings(app: State<App>, settings: Settings) -> Res<()> {
    app.bridge.update_settings(settings).map_err(err)
}

#[tauri::command]
async fn claude_status(app: State<'_, App>) -> Res<claude::AuthStatus> {
    let path = app.bridge.settings.read().claude_path.clone();
    blocking(move || Ok(claude::status(&path))).await
}

#[tauri::command]
fn claude_login(app: State<App>) -> Res<()> {
    claude::login(&app.bridge.settings.read().claude_path).map_err(err)
}

#[tauri::command]
async fn refresh_connections(app: State<'_, App>) -> Res<Vec<String>> {
    let b = app.bridge.clone();
    blocking(move || b.refresh_tools().map(|t| prompt::connector_names(&t))).await
}

#[tauri::command]
async fn sync_now(app: State<'_, App>) -> Res<Value> {
    let b = app.bridge.clone();
    blocking(move || Ok(serde_json::to_value(b.run_now()?)?)).await
}

#[tauri::command]
fn render_preview(app: State<App>, screen: i32, index: i32, paper: bool) -> Res<Value> {
    let s = app.bridge.settings.read().clone();
    let st = app.bridge.status();
    let wire = app.bridge.wire();
    let now = chrono::Local::now().timestamp();
    let opts = preview::FrameOpts {
        now,
        last_sync: st.generated,
        battery: 82,
        charging: 0,
        net: if wire.is_some() { 0 } else { 2 },
        mono: s.mono as i32,
        screen: if wire.is_none() && screen != 3 { 0 } else { screen },
        index,
        disabled: s.disabled_mask(),
    };
    let empty = br#"{"v":1,"gen":1,"cards":[]}"#.to_vec();
    let json = wire.unwrap_or(empty);
    let f = preview::render(&json, &opts).map_err(err)?;
    let png = base64::engine::general_purpose::STANDARD.encode(preview::png(&f, paper));
    Ok(json!({
        "image": format!("data:image/png;base64,{png}"),
        "cards": f.cards,
        "next_change": cink_bridge::service::next_change_local(&json, s.disabled_mask()),
    }))
}

#[tauri::command]
fn list_ports() -> Vec<device::Port> {
    device::ports()
}

#[derive(Deserialize)]
struct Wifi {
    ssid: String,
    pass: String,
}

fn device_config(b: &Bridge, d: &cink_bridge::settings::Device, wifi: Option<&[Wifi]>) -> Value {
    let s = b.settings.read().clone();
    let mut c = json!({
        "token": d.token,
        "key": d.key,
        "bridge_host": format!("{}.local", cink_bridge::service::host_name()),
        "bridge_port": s.port,
        "relay_url": s.relay.as_ref().map(|r| r.url.trim_end_matches('/').to_string()).unwrap_or_default(),
        "refresh_min": s.refresh_min,
        "disabled": s.disabled_mask(),
        "quiet": s.quiet.map(|(a, b)| json!([a, b])).unwrap_or(Value::Null),
        "tz": tz::posix(&tz::iana()),
        "mono": s.mono,
        "time": chrono::Local::now().timestamp(),
    });
    if let Some(w) = wifi {
        c["wifi"] = json!(w.iter().filter(|w| !w.ssid.trim().is_empty())
            .map(|w| json!({"ssid": w.ssid.trim(), "pass": w.pass})).collect::<Vec<_>>());
    }
    c
}

/// First-time setup (or re-pair): pairs keys, sends Wi-Fi and settings, and
/// pushes the current digest so the screen fills in immediately.
#[tauri::command]
async fn setup_device(app: State<'_, App>, port: String, name: String, wifi: Vec<Wifi>) -> Res<Value> {
    let b = app.bridge.clone();
    blocking(move || {
        let mut link = device::Link::open(&port)?;
        let hello = link.hello()?;
        let id = hello["device"].as_str().unwrap_or("").to_string();
        let fw = hello["firmware"].as_str().unwrap_or("").to_string();
        let name = if name.trim().is_empty() { format!("TICKEY {id}") } else { name.trim().to_string() };
        let d = b.pair(&id, &name, &fw)?;
        link.configure(device_config(&b, &d, Some(&wifi)))?;
        let pushed = match b.sealed_for(&d) {
            Some((_, blob)) => link.push_blob(&blob).is_ok(),
            None => false,
        };
        Ok(json!({"device": d.id, "name": d.name, "pushed": pushed}))
    })
    .await
}

/// Re-sends settings (not Wi-Fi) and the latest digest over USB.
#[tauri::command]
async fn update_device(app: State<'_, App>, port: String) -> Res<Value> {
    let b = app.bridge.clone();
    blocking(move || {
        let mut link = device::Link::open(&port)?;
        let hello = link.hello()?;
        let id = hello["device"].as_str().unwrap_or("");
        let d = b.device(id).ok_or_else(|| anyhow::anyhow!("this TICKEY is not paired yet; use Set up first"))?;
        link.configure(device_config(&b, &d, None))?;
        if let Some((_, blob)) = b.sealed_for(&d) {
            link.push_blob(&blob)?;
        }
        Ok(link.status()?)
    })
    .await
}

#[tauri::command]
async fn device_press(port: String, button: String) -> Res<()> {
    blocking(move || device::Link::open(&port)?.press(&button)).await
}

#[tauri::command]
async fn device_frame(port: String, paper: bool) -> Res<String> {
    blocking(move || {
        let (black, accent) = device::Link::open(&port)?.frame()?;
        let f = preview::Frame { black, accent, cards: 0 };
        Ok(format!("data:image/png;base64,{}", base64::engine::general_purpose::STANDARD.encode(preview::png(&f, paper))))
    })
    .await
}

#[tauri::command]
async fn device_reset(port: String) -> Res<()> {
    blocking(move || device::Link::open(&port)?.reset()).await
}

#[tauri::command]
async fn device_board_send(app: State<'_, App>, port: String, profile: Value) -> Res<Value> {
    let saved = profile.clone();
    let r = blocking(move || {
        let mut link = device::Link::open(&port)?;
        link.board(&profile)?;
        link.board_get()
    })
    .await?;
    let mut s = app.bridge.settings.read().clone();
    s.board_profile = saved;
    app.bridge.update_settings(s).map_err(err)?;
    Ok(r)
}

#[tauri::command]
async fn device_board_get(port: String) -> Res<Value> {
    blocking(move || device::Link::open(&port)?.board_get()).await
}

#[tauri::command]
async fn device_board_reset(port: String) -> Res<()> {
    blocking(move || device::Link::open(&port)?.board_reset()).await
}

#[tauri::command]
async fn device_panel_test(port: String) -> Res<Value> {
    blocking(move || device::Link::open(&port)?.panel_test()).await
}

#[tauri::command]
async fn device_learn(port: String, secs: u32, active_low: bool) -> Res<Value> {
    blocking(move || {
        let events = device::Link::open(&port)?.learn(secs, active_low)?;
        let [up, down, back] = device::assign_buttons(&events, active_low);
        Ok(serde_json::json!({"up": up, "down": down, "back": back, "events": events.len()}))
    })
    .await
}

#[tauri::command]
fn forget_device(app: State<App>, id: String) -> Res<()> {
    let mut s = app.bridge.settings.read().clone();
    s.devices.retain(|d| d.id != id);
    app.bridge.update_settings(s).map_err(err)
}

#[tauri::command]
fn preview_prompt(app: State<App>) -> String {
    let s = app.bridge.settings.read().clone();
    prompt::build(&s, chrono::Local::now(), &s.tools, None)
}

#[tauri::command]
fn wire_digest(app: State<App>) -> Option<Value> {
    app.bridge.wire().and_then(|w| serde_json::from_slice(&w).ok())
}

#[tauri::command]
fn get_autostart(app: tauri::AppHandle) -> bool {
    app.autolaunch().is_enabled().unwrap_or(false)
}

#[tauri::command]
fn set_autostart(app: tauri::AppHandle, on: bool) -> Res<()> {
    let a = app.autolaunch();
    if on { a.enable() } else { a.disable() }.map_err(err)
}

#[tauri::command]
fn device_settings_json(app: State<App>) -> Value {
    digest::device_cfg(&app.bridge.settings.read())
}

fn show_main(app: &tauri::AppHandle) {
    if let Some(w) = app.get_webview_window("main") {
        let _ = w.show();
        let _ = w.unminimize();
        let _ = w.set_focus();
    }
}

fn main() {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();
    let bridge = Bridge::new(Settings::load());
    if let Err(e) = bridge.start() {
        log::error!("bridge failed to start: {e:#}");
    }

    tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app, _, _| show_main(app)))
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_autostart::init(tauri_plugin_autostart::MacosLauncher::LaunchAgent, Some(vec!["--background"])))
        .manage(App { bridge: bridge.clone() })
        .setup(move |app| {
            let open = MenuItem::with_id(app, "open", "Open C-Ink Settings", true, None::<&str>)?;
            let sync = MenuItem::with_id(app, "sync", "Sync now", true, None::<&str>)?;
            let quit = MenuItem::with_id(app, "quit", "Quit C-Ink", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&open, &sync, &quit])?;
            let b = bridge.clone();
            TrayIconBuilder::with_id("cink")
                .icon(tauri::image::Image::from_bytes(include_bytes!("../icons/tray.png"))?)
                .icon_as_template(true)
                .tooltip("C-Ink")
                .menu(&menu)
                .on_menu_event(move |app, ev| match ev.id().as_ref() {
                    "open" => show_main(app),
                    "sync" => {
                        let b = b.clone();
                        std::thread::spawn(move || { let _ = b.run_now(); });
                    }
                    "quit" => app.exit(0),
                    _ => {}
                })
                .build(app)?;
            if std::env::args().any(|a| a == "--background") {
                if let Some(w) = app.get_webview_window("main") {
                    let _ = w.hide();
                }
            }
            Ok(())
        })
        .on_window_event(|w, ev| {
            // Closing the window keeps C-Ink syncing from the tray.
            if let WindowEvent::CloseRequested { api, .. } = ev {
                let _ = w.hide();
                api.prevent_close();
            }
        })
        .invoke_handler(tauri::generate_handler![
            get_state, save_settings, claude_status, claude_login, refresh_connections, sync_now,
            render_preview, list_ports, setup_device, update_device, device_press, device_frame,
            device_reset, forget_device, preview_prompt, wire_digest, get_autostart, set_autostart,
            device_settings_json, device_board_send, device_board_get, device_board_reset, device_panel_test,
            device_learn
        ])
        .run(tauri::generate_context!())
        .expect("error while running C-Ink Settings");
}
