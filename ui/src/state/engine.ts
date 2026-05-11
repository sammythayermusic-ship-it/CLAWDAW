// Thin typed wrapper over Tauri's invoke + listen. Centralized so commands
// have one canonical typed signature on the JS side, mirrored against the
// Rust handlers in src-tauri/src/commands.rs.

import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";

import type {
  AddTrackResultDto,
  CommitDto,
  EventDto,
  ProjectDto,
  TrackDto,
  TransportStateDto,
} from "./types";

export const engineApi = {
  getProject: () => invoke<ProjectDto>("get_project"),
  getTrack: (trackId: string) => invoke<TrackDto>("get_track", { trackId }),

  addTrack: (trackType: "AUDIO" | "MIDI" | "AUX" | "BUS" | "FOLDER", name: string) =>
    invoke<AddTrackResultDto>("engine_add_track", { trackType, name }),
  deleteTrack: (trackId: string) =>
    invoke<CommitDto>("engine_delete_track", { trackId }),
  renameTrack: (trackId: string, newName: string) =>
    invoke<CommitDto>("rename_track", { trackId, newName }),
  setTrackVolume: (trackId: string, volumeDb: number) =>
    invoke<CommitDto>("set_track_volume", { trackId, volumeDb }),
  setTrackPan: (trackId: string, pan: number) =>
    invoke<CommitDto>("set_track_pan", { trackId, pan }),
  setPluginParameter: (pluginInstanceId: string, paramId: string, normalized: number) =>
    invoke<CommitDto>("set_plugin_parameter", { pluginInstanceId, paramId, normalized }),
  undo: () => invoke<CommitDto>("engine_undo"),

  play: () => invoke<CommitDto>("engine_play"),
  stop: () => invoke<CommitDto>("engine_stop"),
  getTransportState: () => invoke<TransportStateDto>("engine_get_transport_state"),

  startEventStream: () => invoke<void>("subscribe_events"),

  onEvent: (handler: (e: EventDto) => void): Promise<UnlistenFn> =>
    listen<EventDto>("engine:event", (e) => handler(e.payload)),

  onDisconnected: (handler: () => void): Promise<UnlistenFn> =>
    listen<void>("engine:disconnected", () => handler()),
};
