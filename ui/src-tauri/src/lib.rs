// CLAWDAW UI — Tauri runtime entry point.
// All gRPC interaction with clawdaw_engine flows through `engine::EngineClient`,
// exposed to JS via the commands in `commands.rs`. The streaming SubscribeEvents
// RPC bridges to a tauri event channel (`engine:event`) that the React side
// listens on through `useEngineSync()`.

mod commands;
mod dto;
mod engine;
mod proto;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(engine::EngineClient::new())
        .invoke_handler(tauri::generate_handler![
            commands::get_project,
            commands::get_track,
            commands::engine_add_track,
            commands::engine_delete_track,
            commands::rename_track,
            commands::set_track_volume,
            commands::set_track_pan,
            commands::set_plugin_parameter,
            commands::engine_undo,
            commands::engine_play,
            commands::engine_stop,
            commands::engine_get_transport_state,
            commands::subscribe_events,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
