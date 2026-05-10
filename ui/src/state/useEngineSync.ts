// Hook: connect to the engine, hydrate the store, then keep it live by
// listening for events. Coarse-grained refetch strategy — when an event
// invalidates a track's detail (mixer change, plugin add/remove), the
// reducer drops the cached detail and this hook re-fetches via GetTrack.
//
// Mount once at the App root. Re-mounting is harmless but we don't bother
// guarding against StrictMode double-mounts beyond aborting the in-flight
// connect — duplicate subscribers on the engine side just emit twice.

import { useEffect } from "react";

import { engineApi } from "./engine";
import { useEngineStore } from "./store";

export function useEngineSync() {
  useEffect(() => {
    let cancelled = false;
    const unlisteners: Array<() => void> = [];

    async function connect() {
      useEngineStore.getState().setConnection({ status: "connecting" });
      try {
        const project = await engineApi.getProject();
        if (cancelled) return;

        useEngineStore.getState().hydrateFromProject(project);
        useEngineStore.getState().setConnection({ status: "connected", sinceMs: Date.now() });

        // Wire event handler before starting the stream so we don't drop early events.
        const offEvent = await engineApi.onEvent((event) => {
          useEngineStore.getState().applyEvent(event);
          // Re-fetch the affected track detail when its mirror was invalidated.
          if (event.kind === "trackMixerChanged") {
            void refetchTrack(event.trackId);
          } else if (
            event.kind === "pluginAdded" ||
            event.kind === "pluginRemoved" ||
            event.kind === "pluginParamChanged" ||
            event.kind === "pluginBypassed"
          ) {
            const trackId = "trackId" in event ? event.trackId : useEngineStore.getState().selectedTrackId;
            if (trackId) void refetchTrack(trackId);
          } else if (event.kind === "trackAdded" || event.kind === "trackRemoved") {
            void refetchProject();
          }
        });
        unlisteners.push(offEvent);

        const offDc = await engineApi.onDisconnected(() => {
          useEngineStore
            .getState()
            .setConnection({ status: "disconnected", reason: "stream ended" });
        });
        unlisteners.push(offDc);

        await engineApi.startEventStream();
      } catch (err) {
        if (cancelled) return;
        useEngineStore
          .getState()
          .setConnection({ status: "disconnected", reason: String(err) });
      }
    }

    void connect();

    return () => {
      cancelled = true;
      for (const off of unlisteners) {
        try {
          off();
        } catch {
          // ignore — listener may already be torn down
        }
      }
    };
  }, []);
}

async function refetchTrack(trackId: string) {
  try {
    const detail = await engineApi.getTrack(trackId);
    useEngineStore.getState().setTrackDetails(detail);
  } catch (err) {
    // Engine momentarily unavailable; leave the detail empty and let the
    // next event or user action retry.
    console.warn(`refetchTrack(${trackId}) failed:`, err);
  }
}

async function refetchProject() {
  try {
    const project = await engineApi.getProject();
    useEngineStore.getState().hydrateFromProject(project);
  } catch (err) {
    console.warn("refetchProject failed:", err);
  }
}

// ---------------------------------------------------------------------------
// Convenience hook: ensure the selected track's detail is loaded; trigger a
// fetch if absent. Components subscribe to the detail via the store; this
// hook just kicks off the fetch.
// ---------------------------------------------------------------------------
export function useEnsureSelectedTrackDetail() {
  const selectedId = useEngineStore((s) => s.selectedTrackId);
  const have = useEngineStore((s) => (selectedId ? s.trackDetails[selectedId] : null));

  useEffect(() => {
    if (selectedId && !have) {
      void refetchTrack(selectedId);
    }
  }, [selectedId, have]);
}
