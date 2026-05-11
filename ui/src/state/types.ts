// TypeScript shapes for the DTOs emitted by src-tauri/src/dto.rs.
// Single source of truth for what the JS side sees; if a field is added in
// Rust, mirror it here (and ensure rename_all = "camelCase" stays).

export type TrackTypeName =
  | "UNSPECIFIED"
  | "AUDIO"
  | "MIDI"
  | "AUX"
  | "BUS"
  | "FOLDER"
  | "UNKNOWN";

export type PluginFormatName =
  | "UNSPECIFIED"
  | "VST3"
  | "AU"
  | "INTERNAL"
  | "CLAP"
  | "UNKNOWN";

export interface TrackSummaryDto {
  id: string;
  name: string;
  trackType: TrackTypeName;
  muted: boolean;
  soloed: boolean;
  pluginCount: number;
  regionCount: number;
}

export interface ProjectDto {
  name: string;
  tempoBpm: number;
  sampleRate: number;
  projectPath: string;
  tracks: TrackSummaryDto[];
}

export interface MixerDto {
  volumeDb: number;
  pan: number;
  muted: boolean;
  soloed: boolean;
  peakDb: number;
  rmsDb: number;
}

export interface PluginInstanceDto {
  id: string;
  name: string;
  vendor: string;
  format: PluginFormatName;
  slot: number;
  bypassed: boolean;
  presetName: string;
}

export interface TrackDto {
  id: string;
  name: string;
  trackType: TrackTypeName;
  mixer?: MixerDto;
  plugins: PluginInstanceDto[];
  armedForRecord: boolean;
}

export interface CommitDto {
  commitId: string;
  description: string;
}

export interface AddTrackResultDto {
  trackId: string;
  commitId: string;
  description: string;
}

export interface TransportStateDto {
  playing: boolean;
  recording: boolean;
  positionSeconds: number;
}

// Tagged union — `kind` is the discriminant. Match those against the
// rename_all = "camelCase" output of dto.rs::EventDto.
export type EventDto =
  | { kind: "trackAdded"; trackId: string }
  | { kind: "trackRemoved"; trackId: string }
  | { kind: "trackRenamed"; trackId: string; newName: string }
  | { kind: "trackMixerChanged"; trackId: string }
  | { kind: "pluginAdded"; pluginInstanceId: string; trackId: string }
  | { kind: "pluginRemoved"; pluginInstanceId: string }
  | {
      kind: "pluginParamChanged";
      pluginInstanceId: string;
      paramId: string;
      newValue: number;
    }
  | { kind: "pluginBypassed"; pluginInstanceId: string; bypassed: boolean }
  | { kind: "tempoChanged"; newTempoBpm: number }
  | { kind: "commandApplied"; commitId: string; description: string; source: string }
  | { kind: "commandUndone"; commitId: string; description: string }
  | { kind: "other"; kindName: string };
