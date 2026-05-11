// Tauri command handlers — thin wrappers around engine RPCs that translate
// prost types to serde DTOs. Every command returns Result<T, String> so the
// JS side gets either a typed payload or a human-readable error string.

use tauri::{Emitter, State};
use tonic::Request;

use crate::dto::{AddTrackResultDto, CommitDto, EventDto, ProjectDto, TrackDto, TransportStateDto};
use crate::engine::{EngineClient, UNARY_RPC_TIMEOUT};
use crate::proto;

/// Build a tonic Request with the unary-RPC deadline applied. We do this
/// per-call instead of at the channel level so the streaming SubscribeEvents
/// RPC isn't cancelled after the same timeout — see engine.rs.
fn unary<T>(body: T) -> Request<T> {
    let mut req = Request::new(body);
    req.set_timeout(UNARY_RPC_TIMEOUT);
    req
}

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------
#[tauri::command]
pub async fn get_project(engine: State<'_, EngineClient>) -> Result<ProjectDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .get_project(unary(()))
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
        .get_track(unary(proto::GetTrackRequest { track_id }))
        .await
        .map_err(|e| format!("GetTrack: {e}"))?;
    Ok(resp.into_inner().into())
}

// ---------------------------------------------------------------------------
// Mutations — all return CommitDto for undo bookkeeping on the JS side
// ---------------------------------------------------------------------------
#[tauri::command]
pub async fn engine_add_track(
    engine: State<'_, EngineClient>,
    track_type: String,
    name: String,
) -> Result<AddTrackResultDto, String> {
    // Map the JS-side enum name ("AUDIO") to the proto TrackType int. v1
    // only supports AUDIO; anything else is forwarded as-is and the engine
    // returns INVALID_ARGUMENT.
    let type_int = match track_type.as_str() {
        "AUDIO" => proto::TrackType::Audio as i32,
        "MIDI" => proto::TrackType::Midi as i32,
        "AUX" => proto::TrackType::Aux as i32,
        "BUS" => proto::TrackType::Bus as i32,
        "FOLDER" => proto::TrackType::Folder as i32,
        _ => proto::TrackType::Unspecified as i32,
    };
    let mut client = engine.client().await?;
    let resp = client
        .add_track(unary(proto::AddTrackRequest {
            r#type: type_int,
            name,
            color: None,
            insert_at_index: 0,
        }))
        .await
        .map_err(|e| format!("AddTrack: {e}"))?;
    Ok(resp.into_inner().into())
}

#[tauri::command]
pub async fn engine_delete_track(
    engine: State<'_, EngineClient>,
    track_id: String,
) -> Result<CommitDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .delete_track(unary(proto::DeleteTrackRequest {
            track_id,
            confirm: true,
        }))
        .await
        .map_err(|e| format!("DeleteTrack: {e}"))?;
    Ok(resp.into_inner().into())
}

#[tauri::command]
pub async fn rename_track(
    engine: State<'_, EngineClient>,
    track_id: String,
    new_name: String,
) -> Result<CommitDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .rename_track(unary(proto::RenameTrackRequest {
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
        .set_track_volume(unary(proto::SetTrackVolumeRequest {
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
        .set_track_pan(unary(proto::SetTrackPanRequest { track_id, pan }))
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
        .set_plugin_parameter(unary(proto::SetPluginParameterRequest {
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
        .undo(unary(proto::UndoRequest::default()))
        .await
        .map_err(|e| format!("Undo: {e}"))?;
    Ok(resp.into_inner().into())
}

// ---------------------------------------------------------------------------
// Transport — Play/Stop mutations + state read. The UI updates optimistically
// from the button onClick rather than waiting for an event, so the round-trip
// here is just a fire-and-forget mutation as far as the user sees.
// ---------------------------------------------------------------------------
#[tauri::command]
pub async fn engine_play(engine: State<'_, EngineClient>) -> Result<CommitDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .play(unary(()))
        .await
        .map_err(|e| format!("Play: {e}"))?;
    Ok(resp.into_inner().into())
}

#[tauri::command]
pub async fn engine_stop(engine: State<'_, EngineClient>) -> Result<CommitDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .stop(unary(()))
        .await
        .map_err(|e| format!("Stop: {e}"))?;
    Ok(resp.into_inner().into())
}

#[tauri::command]
pub async fn engine_get_transport_state(
    engine: State<'_, EngineClient>,
) -> Result<TransportStateDto, String> {
    let mut client = engine.client().await?;
    let resp = client
        .get_transport_state(unary(()))
        .await
        .map_err(|e| format!("GetTransportState: {e}"))?;
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
    // No timeout on this Request — server-streaming RPCs would otherwise
    // be cancelled by the per-RPC deadline. See engine.rs::UNARY_RPC_TIMEOUT.
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
