// Hook: connect to the engine, hydrate the store, then keep it live by
// listening for events. Coarse-grained refetch strategy — when an event
// invalidates a track's detail (mixer change, plugin add/remove), the
// reducer drops the cached detail and this hook re-fetches via GetTrack.
//
// Auto-reconnect: if the stream errors out (engine restart, transient gRPC
// failure, etc.), we schedule a retry with exponential backoff capped at
// 30s. Mount once at the App root.

import { useEffect } from "react";

import { engineApi } from "./engine";
import { useEngineStore } from "./store";

const RETRY_BASE_MS = 1000;
const RETRY_CAP_MS = 30_000;

export function useEngineSync() {
  useEffect(() => {
    let cancelled = false;
    let retryTimer: ReturnType<typeof setTimeout> | null = null;
    let attempt = 0;
    let unlisteners: Array<() => void> = [];

    function clearUnlisteners() {
      for (const off of unlisteners) {
        try {
          off();
        } catch {
          // ignore — listener may already be torn down
        }
      }
      unlisteners = [];
    }

    function scheduleRetry(reason: string) {
      if (cancelled) return;
      if (retryTimer) return; // already scheduled
      const delay = Math.min(RETRY_BASE_MS * 2 ** attempt, RETRY_CAP_MS);
      attempt += 1;
      console.warn(`[engine] reconnect attempt ${attempt} in ${delay}ms (${reason})`);
      retryTimer = setTimeout(() => {
        retryTimer = null;
        clearUnlisteners();
        void connect();
      }, delay);
    }

    async function connect() {
      if (cancelled) return;
      useEngineStore.getState().setConnection({ status: "connecting" });
      try {
        const project = await engineApi.getProject();
        if (cancelled) return;
        attempt = 0; // success — reset backoff
        useEngineStore.getState().hydrateFromProject(project);
        useEngineStore.getState().setConnection({ status: "connected", sinceMs: Date.now() });

        // Wire event handler before starting the stream so we don't drop early events.
        const offEvent = await engineApi.onEvent((event) => {
          useEngineStore.getState().applyEvent(event);
          if (event.kind === "trackMixerChanged") {
            void refetchTrack(event.trackId);
          } else if (
            event.kind === "pluginAdded" ||
            event.kind === "pluginRemoved" ||
            event.kind === "pluginParamChanged" ||
            event.kind === "pluginBypassed"
          ) {
            const trackId =
              "trackId" in event ? event.trackId : useEngineStore.getState().selectedTrackId;
            if (trackId) void refetchTrack(trackId);
          } else if (event.kind === "trackAdded" || event.kind === "trackRemoved") {
            void refetchProject();
          }
        });
        unlisteners.push(offEvent);

        const offDc = await engineApi.onDisconnected(() => {
          if (cancelled) return;
          useEngineStore
            .getState()
            .setConnection({ status: "disconnected", reason: "stream ended" });
          scheduleRetry("stream ended");
        });
        unlisteners.push(offDc);

        await engineApi.startEventStream();
      } catch (err) {
        if (cancelled) return;
        const reason = String(err);
        useEngineStore.getState().setConnection({ status: "disconnected", reason });
        scheduleRetry(reason);
      }
    }

    void connect();

    return () => {
      cancelled = true;
      if (retryTimer) {
        clearTimeout(retryTimer);
        retryTimer = null;
      }
      clearUnlisteners();
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
