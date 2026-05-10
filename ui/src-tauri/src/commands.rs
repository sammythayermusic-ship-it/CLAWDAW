// Tauri command handlers — thin wrappers around engine RPCs that translate
// prost types to serde DTOs. Every command returns Result<T, String> so the
// JS side gets either a typed payload or a human-readable error string.

use tauri::{Emitter, State};
use tonic::Request;

use crate::dto::{CommitDto, EventDto, ProjectDto, TrackDto};
use crate::engine::EngineClient;
use crate::proto;

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------
#[tauri::command]
pub async fn get_project(engine: State<'_, EngineClient>) -> Result<ProjectDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .get_project(Request::new(()))
        .await
        .map_err(|e| format!("GetProject: {e}"))?;
    Ok(resp.into_inner().into())
}

#[tauri::command]
pub async fn get_track(
    engine: State<'_, EngineClient>,
    track_id: String,
) -> Result<TrackDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .get_track(Request::new(proto::GetTrackRequest { track_id }))
        .await
        .map_err(|e| format!("GetTrack: {e}"))?;
    Ok(resp.into_inner().into())
}

// ---------------------------------------------------------------------------
// Mutations — all return CommitDto for undo bookkeeping on the JS side
// ---------------------------------------------------------------------------
#[tauri::command]
pub async fn rename_track(
    engine: State<'_, EngineClient>,
    track_id: String,
    new_name: String,
) -> Result<CommitDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .rename_track(Request::new(proto::RenameTrackRequest {
            track_id,
            name: new_name,
        }))
        .await
        .map_err(|e| format!("RenameTrack: {e}"))?;
    Ok(resp.into_inner().into())
}

#[tauri::command]
pub async fn set_track_volume(
    engine: State<'_, EngineClient>,
    track_id: String,
    volume_db: f64,
) -> Result<CommitDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .set_track_volume(Request::new(proto::SetTrackVolumeRequest {
            track_id,
            volume_db,
        }))
        .await
        .map_err(|e| format!("SetTrackVolume: {e}"))?;
    Ok(resp.into_inner().into())
}

#[tauri::command]
pub async fn set_track_pan(
    engine: State<'_, EngineClient>,
    track_id: String,
    pan: f64,
) -> Result<CommitDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .set_track_pan(Request::new(proto::SetTrackPanRequest { track_id, pan }))
        .await
        .map_err(|e| format!("SetTrackPan: {e}"))?;
    Ok(resp.into_inner().into())
}

#[tauri::command]
pub async fn set_plugin_parameter(
    engine: State<'_, EngineClient>,
    plugin_instance_id: String,
    param_id: String,
    normalized: f64,
) -> Result<CommitDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .set_plugin_parameter(Request::new(proto::SetPluginParameterRequest {
            plugin_instance_id,
            param_id,
            value: Some(proto::set_plugin_parameter_request::Value::Normalized(
                normalized,
            )),
        }))
        .await
        .map_err(|e| format!("SetPluginParameter: {e}"))?;
    Ok(resp.into_inner().into())
}

#[tauri::command]
pub async fn engine_undo(engine: State<'_, EngineClient>) -> Result<CommitDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .undo(Request::new(proto::UndoRequest::default()))
        .await
        .map_err(|e| format!("Undo: {e}"))?;
    Ok(resp.into_inner().into())
}

// ---------------------------------------------------------------------------
// Streaming SubscribeEvents — spawn a tokio task that forwards each Event
// to the Tauri event channel "engine:event". The task lives for the window's
// lifetime; if the stream errors out we log to stderr and return.
// ---------------------------------------------------------------------------
#[tauri::command]
pub async fn subscribe_events(
    engine: State<'_, EngineClient>,
    app: tauri::AppHandle,
) -> Result<(), String> {
    let mut client = engine.client().await?;
    let stream = client
        .subscribe_events(Request::new(proto::EventFilter::default()))
        .await
        .map_err(|e| format!("SubscribeEvents: {e}"))?
        .into_inner();

    tauri::async_runtime::spawn(async move {
        let mut stream = stream;
        loop {
            match stream.message().await {
                Ok(Some(event)) => {
                    let dto: EventDto = event.into();
                    if let Err(e) = app.emit("engine:event", dto) {
                        eprintln!("[engine:event] emit failed: {e}");
                    }
                }
                Ok(None) => {
                    eprintln!("[engine:event] stream closed by server");
                    let _ = app.emit("engine:disconnected", ());
                    break;
                }
                Err(e) => {
                    eprintln!("[engine:event] stream error: {e}");
                    let _ = app.emit("engine:disconnected", ());
                    break;
                }
            }
        }
    });

    Ok(())
}
