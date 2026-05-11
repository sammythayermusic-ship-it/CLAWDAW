#!/usr/bin/env bash
# Regenerate Python gRPC stubs from ../proto/daw/v1/*.proto into ./generated/.
#
# We use grpc_tools.protoc from the agent's venv (rather than `buf generate`)
# because the buf cloud `protocolbuffers/python` plugin currently emits
# gencode targeting protobuf 7.34.1, which has no matching runtime on PyPI
# yet. The venv's grpcio-tools matches its protobuf runtime, so versions stay
# consistent.
#
# Usage:
#   cd agent
#   ./scripts/regen-proto.sh

set -euo pipefail

cd "$(dirname "$0")/.."

PROTO_ROOT="../proto"
OUT="generated"

mkdir -p "$OUT"

protos=(
  "$PROTO_ROOT/daw/v1/common.proto"
  "$PROTO_ROOT/daw/v1/project.proto"
  "$PROTO_ROOT/daw/v1/plugin.proto"
  "$PROTO_ROOT/daw/v1/transport.proto"
  "$PROTO_ROOT/daw/v1/mixer.proto"
  "$PROTO_ROOT/daw/v1/analysis.proto"
  "$PROTO_ROOT/daw/v1/events.proto"
  "$PROTO_ROOT/daw/v1/engine.proto"
)

uv run python -m grpc_tools.protoc \
  -I"$PROTO_ROOT" \
  --python_out="$OUT" \
  --grpc_python_out="$OUT" \
  "${protos[@]}"

# Package markers (gitignored alongside the generated *.py).
touch "$OUT/daw/__init__.py" "$OUT/daw/v1/__init__.py"

echo "regenerated $(ls $OUT/daw/v1/*.py | wc -l | tr -d ' ') python files in $OUT/daw/v1/"
