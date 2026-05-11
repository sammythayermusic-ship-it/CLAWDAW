// Serde-friendly DTO mirrors of the proto types.
// Why not just send the prost-generated structs? Tauri IPC expects serde
// Serialize, prost types don't derive it, and the proto enum-as-i32 +
// oneof-as-Option<Enum> shapes are noisy on the JS side. These DTOs flatten
// to clean JSON: TrackSummaryDto.type is a string like "AUDIO", events are
// a `{ kind: "trackRenamed", trackId, newName }` tagged-union.

use serde::Serialize;

use crate::proto;

// ---------------------------------------------------------------------------
// Project / Track
// ---------------------------------------------------------------------------
#[derive(Serialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct ProjectDto {
    pub name: String,
    pub tempo_bpm: f64,
    pub sample_rate: u32,
    pub project_path: String,
    pub tracks: Vec<TrackSummaryDto>,
}

#[derive(Serialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct TrackSummaryDto {
    pub id: String,
    pub name: String,
    pub track_type: String,
    pub muted: bool,
    pub soloed: bool,
    pub plugin_count: u32,
    pub region_count: u32,
}

#[derive(Serialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct TrackDto {
    pub id: String,
    pub name: String,
    pub track_type: String,
    pub mixer: Option<MixerDto>,
    pub plugins: Vec<PluginInstanceDto>,
    pub armed_for_record: bool,
}

#[derive(Serialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct MixerDto {
    pub volume_db: f64,
    pub pan: f64,
    pub muted: bool,
    pub soloed: bool,
    pub peak_db: f64,
    pub rms_db: f64,
}

#[derive(Serialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct PluginInstanceDto {
    pub id: String,
    pub name: String,
    pub vendor: String,
    pub format: String,
    pub slot: u32,
    pub bypassed: bool,
    pub preset_name: String,
}

#[derive(Serialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct CommitDto {
    pub commit_id: String,
    pub description: String,
}

#[derive(Serialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct AddTrackResultDto {
    pub track_id: String,
    pub commit_id: String,
    pub description: String,
}

#[derive(Serialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct TransportStateDto {
    pub playing: bool,
    pub recording: bool,
    pub position_seconds: f64,
}

// ---------------------------------------------------------------------------
// Events — tagged union, friendly for `switch (event.kind)` on the JS side.
//   The wire shape is `{ kind, ...fields }`. We omit the `event_id` and
//   timestamp from the payload since the UI doesn't need them yet; add later.
// ---------------------------------------------------------------------------
#[derive(Serialize, Clone, Debug)]
#[serde(tag = "kind", rename_all = "camelCase")]
pub enum EventDto {
    #[serde(rename_all = "camelCase")]
    TrackAdded { track_id: String },
    #[serde(rename_all = "camelCase")]
    TrackRemoved { track_id: String },
    #[serde(rename_all = "camelCase")]
    TrackRenamed {
        track_id: String,
        new_name: String,
    },
    #[serde(rename_all = "camelCase")]
    TrackMixerChanged { track_id: String },
    #[serde(rename_all = "camelCase")]
    PluginAdded {
        plugin_instance_id: String,
        track_id: String,
    },
    #[serde(rename_all = "camelCase")]
    PluginRemoved { plugin_instance_id: String },
    #[serde(rename_all = "camelCase")]
    PluginParamChanged {
        plugin_instance_id: String,
        param_id: String,
        new_value: f64,
    },
    #[serde(rename_all = "camelCase")]
    PluginBypassed {
        plugin_instance_id: String,
        bypassed: bool,
    },
    #[serde(rename_all = "camelCase")]
    TempoChanged { new_tempo_bpm: f64 },
    #[serde(rename_all = "camelCase")]
    CommandApplied {
        commit_id: String,
        description: String,
        source: String,
    },
    #[serde(rename_all = "camelCase")]
    CommandUndone {
        commit_id: String,
        description: String,
    },
    /// Catch-all for event variants we haven't surfaced yet — we still emit
    /// it so the JS side can log "unhandled" and keep going.
    #[serde(rename_all = "camelCase")]
    Other { kind_name: String },
}

// ---------------------------------------------------------------------------
// Conversions from prost types to DTOs
// ---------------------------------------------------------------------------
fn track_type_name(t: i32) -> String {
    // Note: Tracktion has internal track types (Marker/Tempo/Chord/Arranger)
    // that the proto enum doesn't expose — the engine maps those to UNSPECIFIED.
    match proto::TrackType::try_from(t) {
        Ok(proto::TrackType::Unspecified) => "UNSPECIFIED".into(),
        Ok(proto::TrackType::Audio) => "AUDIO".into(),
        Ok(proto::TrackType::Midi) => "MIDI".into(),
        Ok(proto::TrackType::Aux) => "AUX".into(),
        Ok(proto::TrackType::Bus) => "BUS".into(),
        Ok(proto::TrackType::Folder) => "FOLDER".into(),
        Err(_) => "UNKNOWN".into(),
    }
}

fn plugin_format_name(f: i32) -> String {
    match proto::PluginFormat::try_from(f) {
        Ok(proto::PluginFormat::Unspecified) => "UNSPECIFIED".into(),
        Ok(proto::PluginFormat::Vst3) => "VST3".into(),
        Ok(proto::PluginFormat::Au) => "AU".into(),
        Ok(proto::PluginFormat::Internal) => "INTERNAL".into(),
        Ok(proto::PluginFormat::Clap) => "CLAP".into(),
        Err(_) => "UNKNOWN".into(),
    }
}

impl From<proto::TrackSummary> for TrackSummaryDto {
    fn from(t: proto::TrackSummary) -> Self {
        Self {
            id: t.id,
            name: t.name,
            track_type: track_type_name(t.r#type),
            muted: t.muted,
            soloed: t.soloed,
            plugin_count: t.plugin_count,
            region_count: t.region_count,
        }
    }
}

impl From<proto::Project> for ProjectDto {
    fn from(p: proto::Project) -> Self {
        Self {
            name: p.name,
            tempo_bpm: p.tempo_bpm,
            sample_rate: p.sample_rate,
            project_path: p.project_path,
            tracks: p.tracks.into_iter().map(Into::into).collect(),
        }
    }
}

impl From<proto::Mixer> for MixerDto {
    fn from(m: proto::Mixer) -> Self {
        Self {
            volume_db: m.volume_db,
            pan: m.pan,
            muted: m.muted,
            soloed: m.soloed,
            peak_db: m.peak_db,
            rms_db: m.rms_db,
        }
    }
}

impl From<proto::PluginInstance> for PluginInstanceDto {
    fn from(p: proto::PluginInstance) -> Self {
        let info = p.info.unwrap_or_default();
        Self {
            id: p.id,
            name: info.name,
            vendor: info.vendor,
            format: plugin_format_name(info.format),
            slot: p.slot,
            bypassed: p.bypassed,
            preset_name: p.preset_name,
        }
    }
}

impl From<proto::Track> for TrackDto {
    fn from(t: proto::Track) -> Self {
        Self {
            id: t.id,
            name: t.name,
            track_type: track_type_name(t.r#type),
            mixer: t.mixer.map(Into::into),
            plugins: t.plugins.into_iter().map(Into::into).collect(),
            armed_for_record: t.armed_for_record,
        }
    }
}

impl From<proto::MutationResult> for CommitDto {
    fn from(m: proto::MutationResult) -> Self {
        Self {
            commit_id: m.commit_id,
            description: m.description,
        }
    }
}

impl From<proto::AddTrackResponse> for AddTrackResultDto {
    fn from(r: proto::AddTrackResponse) -> Self {
        let mut commit_id = String::new();
        let mut description = String::new();
        if let Some(m) = r.mutation {
            commit_id = m.commit_id;
            description = m.description;
        }
        Self {
            track_id: r.track_id,
            commit_id,
            description,
        }
    }
}

impl From<proto::TransportState> for TransportStateDto {
    fn from(t: proto::TransportState) -> Self {
        Self {
            playing: t.playing,
            recording: t.recording,
            position_seconds: t.position.map(|p| p.seconds).unwrap_or(0.0),
        }
    }
}

impl From<proto::Event> for EventDto {
    fn from(e: proto::Event) -> Self {
        use proto::event::Payload;
        match e.payload {
            Some(Payload::TrackAdded(p)) => EventDto::TrackAdded { track_id: p.track_id },
            Some(Payload::TrackRemoved(p)) => EventDto::TrackRemoved { track_id: p.track_id },
            Some(Payload::TrackRenamed(p)) => EventDto::TrackRenamed {
                track_id: p.track_id,
                new_name: p.new_name,
            },
            Some(Payload::TrackMixerChanged(p)) => EventDto::TrackMixerChanged {
                track_id: p.track_id,
            },
            Some(Payload::PluginAdded(p)) => EventDto::PluginAdded {
                plugin_instance_id: p.plugin_instance_id,
                track_id: p.track_id,
            },
            Some(Payload::PluginRemoved(p)) => EventDto::PluginRemoved {
                plugin_instance_id: p.plugin_instance_id,
            },
            Some(Payload::PluginParamChanged(p)) => EventDto::PluginParamChanged {
                plugin_instance_id: p.plugin_instance_id,
                param_id: p.param_id,
                new_value: p.new_value,
            },
            Some(Payload::PluginBypassed(p)) => EventDto::PluginBypassed {
                plugin_instance_id: p.plugin_instance_id,
                bypassed: p.bypassed,
            },
            Some(Payload::TempoChanged(p)) => EventDto::TempoChanged {
                new_tempo_bpm: p.new_tempo_bpm,
            },
            Some(Payload::CommandApplied(p)) => EventDto::CommandApplied {
                commit_id: p.commit_id,
                description: p.description,
                source: p.source,
            },
            Some(Payload::CommandUndone(p)) => EventDto::CommandUndone {
                commit_id: p.commit_id,
                description: p.description,
            },
            Some(other) => EventDto::Other {
                kind_name: format!("{other:?}").split_whitespace().next().unwrap_or("Unknown").to_string(),
            },
            None => EventDto::Other { kind_name: "Empty".into() },
        }
    }
}
