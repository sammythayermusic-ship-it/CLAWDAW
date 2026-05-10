// Tonic-based gRPC client for clawdaw_engine.
//
// Connection model: lazy. We hold a tokio::Mutex<Option<Channel>> and connect
// on first use; later commands reuse the same channel (tonic pools HTTP/2
// streams under the hood). If the engine restarts, the channel breaks and the
// next command surfaces the error to the JS side; we don't auto-reconnect yet.
// (`reconnect()` exists below for when we add a "Reconnect" button.)

use std::time::Duration;

use tokio::sync::Mutex;
use tonic::transport::{Channel, Endpoint};

use crate::proto;

const ENGINE_ENDPOINT: &str = "http://127.0.0.1:50051";
const CONNECT_TIMEOUT: Duration = Duration::from_secs(2);
const RPC_TIMEOUT: Duration = Duration::from_secs(8);

pub struct EngineClient {
    channel: Mutex<Option<Channel>>,
}

impl EngineClient {
    pub fn new() -> Self {
        Self {
            channel: Mutex::new(None),
        }
    }

    async fn channel(&self) -> Result<Channel, String> {
        let mut guard = self.channel.lock().await;
        if let Some(ch) = guard.as_ref() {
            return Ok(ch.clone());
        }
        let endpoint = Endpoint::from_static(ENGINE_ENDPOINT)
            .connect_timeout(CONNECT_TIMEOUT)
            .timeout(RPC_TIMEOUT);
        let ch = endpoint
            .connect()
            .await
            .map_err(|e| format!("connect to {ENGINE_ENDPOINT}: {e}"))?;
        *guard = Some(ch.clone());
        Ok(ch)
    }

    pub async fn reconnect(&self) -> Result<(), String> {
        *self.channel.lock().await = None;
        let _ = self.channel().await?;
        Ok(())
    }

    /// Returns a fresh tonic client over the cached channel. tonic clients
    /// need `&mut self` so we hand out owned clones per call.
    pub async fn client(&self) -> Result<proto::EngineClient<Channel>, String> {
        Ok(proto::EngineClient::new(self.channel().await?))
    }
}
