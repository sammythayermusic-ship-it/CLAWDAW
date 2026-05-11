// Generates Rust types and a tonic gRPC client from the engine proto contract.
// Output goes to OUT_DIR (Cargo's per-target build directory) and is included
// from src/proto.rs via `tonic::include_proto!("daw.v1")`. We do NOT vendor
// generated code into the repo — proto/ is the source of truth, regen on build.

use std::path::PathBuf;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let proto_root: PathBuf = std::path::Path::new("../../proto").canonicalize()?;
    let proto_files = [
        "daw/v1/analysis.proto",
        "daw/v1/common.proto",
        "daw/v1/engine.proto",
        "daw/v1/events.proto",
        "daw/v1/mixer.proto",
        "daw/v1/plugin.proto",
        "daw/v1/project.proto",
        "daw/v1/transport.proto",
    ];

    tonic_build::configure()
        .build_server(false)
        .build_client(true)
        .compile_protos(&proto_files, &[proto_root.to_str().unwrap()])?;

    // Re-run build if any proto changes.
    for f in proto_files {
        println!("cargo:rerun-if-changed={}", proto_root.join(f).display());
    }

    tauri_build::build();
    Ok(())
}
