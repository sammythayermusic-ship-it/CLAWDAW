// Engine-mirroring store. Per CLAUDE.md state-management rule: the engine is
// the source of truth, the UI never holds state that isn't either ephemeral
// interaction state or a mirror of engine state populated by GetProject +
// SubscribeEvents. Mutations round-trip through the engine; the resulting
// event refreshes the mirror. We do NOT optimistic-update.
//
// Reducers below are deliberately small. For TrackMixerChanged we re-fetch
// the track via GetTrack, since the event itself carries no values; for
// TrackRenamed the new name is on the event so we patch in place.

import { create } from "zustand";

import type { CommitDto, EventDto, ProjectDto, TrackDto, TrackSummaryDto } from "./types";

// ---------------------------------------------------------------------------
// Connection state
// ---------------------------------------------------------------------------
export type ConnectionState =
  | { status: "idle" }
  | { status: "connecting" }
  | { status: "connected"; sinceMs: number }
  | { status: "disconnected"; reason: string };

// ---------------------------------------------------------------------------
// Store shape
// ---------------------------------------------------------------------------
interface EngineStoreState {
  connection: ConnectionState;
  project: ProjectDto | null;
  // tracks indexed by id for cheap reducer updates; we also derive ordered
  // arrays for UI.
  tracksById: Record<string, TrackSummaryDto>;
  trackOrder: string[];
  // Fully-fetched track details, keyed by id. Populated lazily when a track
  // becomes the selection; refreshed on TrackMixerChanged / PluginAdded etc.
  trackDetails: Record<string, TrackDto>;
  selectedTrackId: string | null;
  recentCommits: CommitDto[]; // newest first, capped
  // Transport: updated optimistically from the TransportBar onClick. The
  // engine doesn't emit TransportStateChanged events yet — when a second
  // caller (e.g. the agent) can race with the UI we'll plumb the event
  // and switch this to a mirror.
  transport: { isPlaying: boolean };

  // Actions
  setConnection: (c: ConnectionState) => void;
  hydrateFromProject: (p: ProjectDto) => void;
  setSelectedTrack: (id: string | null) => void;
  setTrackDetails: (t: TrackDto) => void;
  setTransportPlaying: (playing: boolean) => void;
  applyEvent: (e: EventDto) => void;
}

const RECENT_COMMITS_CAP = 50;

// Pure helper: derive ordered track list from a project's tracks array,
// filtering out the synthetic engine tracks (Marker/Tempo/Chord/Arranger)
// that the proto enum can't name. They're returned by GetProject as type
// UNSPECIFIED — we hide them from the visible track list since the user
// didn't create them.
function visibleTrackOrder(tracks: TrackSummaryDto[]): string[] {
  return tracks.filter(isUserVisible).map((t) => t.id);
}

function isUserVisible(t: TrackSummaryDto): boolean {
  if (t.trackType === "AUDIO" || t.trackType === "MIDI" || t.trackType === "BUS" || t.trackType === "AUX" || t.trackType === "FOLDER") {
    return true;
  }
  // Tracktion's internal management tracks come back as UNSPECIFIED with
  // names like "Marker", "Tempo", "Chord", "Arranger". Hide those.
  return false;
}

export const useEngineStore = create<EngineStoreState>((set, get) => ({
  connection: { status: "idle" },
  project: null,
  tracksById: {},
  trackOrder: [],
  trackDetails: {},
  selectedTrackId: null,
  recentCommits: [],
  transport: { isPlaying: false },

  setConnection: (c) => set({ connection: c }),

  setTransportPlaying: (playing) => set({ transport: { isPlaying: playing } }),

  hydrateFromProject: (p) => {
    const byId: Record<string, TrackSummaryDto> = {};
    for (const t of p.tracks) byId[t.id] = t;
    const order = visibleTrackOrder(p.tracks);
    set({
      project: p,
      tracksById: byId,
      trackOrder: order,
      // If selection is gone or never set, default to the first visible track.
      selectedTrackId: get().selectedTrackId && byId[get().selectedTrackId!] ? get().selectedTrackId : order[0] ?? null,
    });
  },

  setSelectedTrack: (id) => set({ selectedTrackId: id }),

  setTrackDetails: (t) => set((s) => ({ trackDetails: { ...s.trackDetails, [t.id]: t } })),

  applyEvent: (e) => {
    switch (e.kind) {
      case "trackRenamed": {
        set((s) => {
          const existing = s.tracksById[e.trackId];
          if (!existing) return s;
          const next = { ...existing, name: e.newName };
          const detailExisting = s.trackDetails[e.trackId];
          return {
            tracksById: { ...s.tracksById, [e.trackId]: next },
            trackDetails: detailExisting
              ? { ...s.trackDetails, [e.trackId]: { ...detailExisting, name: e.newName } }
              : s.trackDetails,
          };
        });
        break;
      }

      case "trackMixerChanged": {
        // Mark the track detail as stale so the consumer can refetch.
        // The actual GetTrack call lives in useEngineSync.
        set((s) => {
          if (!s.trackDetails[e.trackId]) return s;
          // We don't have new values on the event — clear the detail to
          // signal "needs refresh", and let the hook re-fetch.
          const next = { ...s.trackDetails };
          delete next[e.trackId];
          return { trackDetails: next };
        });
        break;
      }

      case "trackAdded":
      case "trackRemoved": {
        // Coarse-grained: the track set changed. Signal the hook to re-fetch
        // GetProject. We can't reorder accurately from just the event.
        set((s) => ({ project: s.project /* hook listens for these and re-fetches */ }));
        break;
      }

      case "pluginAdded":
      case "pluginRemoved":
      case "pluginParamChanged":
      case "pluginBypassed": {
        // The plugin chain for the affected track is now stale. Drop the
        // detail; the panel re-fetches via GetTrack on next render.
        const trackId =
          "trackId" in e
            ? (e as { trackId: string }).trackId
            : findTrackIdFromPluginInstance(get().trackDetails, e.pluginInstanceId);
        if (!trackId) return;
        set((s) => {
          const next = { ...s.trackDetails };
          delete next[trackId];
          return { trackDetails: next };
        });
        break;
      }

      case "tempoChanged": {
        set((s) => (s.project ? { project: { ...s.project, tempoBpm: e.newTempoBpm } } : s));
        break;
      }

      case "commandApplied": {
        const commit: CommitDto = { commitId: e.commitId, description: e.description };
        set((s) => ({
          recentCommits: [commit, ...s.recentCommits].slice(0, RECENT_COMMITS_CAP),
        }));
        break;
      }

      case "commandUndone":
      case "other":
      default:
        break;
    }
  },
}));

function findTrackIdFromPluginInstance(
  details: Record<string, TrackDto>,
  pluginInstanceId: string,
): string | null {
  for (const trackId of Object.keys(details)) {
    if (details[trackId].plugins.some((p) => p.id === pluginInstanceId)) return trackId;
  }
  return null;
}

// ---------------------------------------------------------------------------
// Selectors — encourage components to subscribe to slim slices.
// ---------------------------------------------------------------------------
export const selectVisibleTracks = (s: EngineStoreState): TrackSummaryDto[] =>
  s.trackOrder.map((id) => s.tracksById[id]).filter(Boolean);

export const selectSelectedTrackDetail = (s: EngineStoreState): TrackDto | null =>
  s.selectedTrackId ? s.trackDetails[s.selectedTrackId] ?? null : null;
