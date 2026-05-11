// All tonic-generated proto code lands here.
// build.rs runs `tonic_build::configure().compile_protos(...)` against
// ../../proto/daw/v1/*.proto, OUT_DIR receives daw.v1.rs.
// Re-export the EngineClient at this module's root for convenience.

#![allow(clippy::all, clippy::pedantic, dead_code)]

pub mod daw {
    pub mod v1 {
        tonic::include_proto!("daw.v1");
    }
}

pub use daw::v1::engine_client::EngineClient;
pub use daw::v1::*;
