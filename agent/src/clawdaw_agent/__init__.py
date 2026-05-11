"""CLAWDAW MCP agent.

Exposes the audio engine's gRPC surface as MCP tools so Claude can talk to it.

The generated gRPC stubs live in `agent/generated/daw/v1/` and are installed
into the environment as a sibling top-level `daw` package via hatchling
(see `[tool.hatch.build.targets.wheel].packages` in pyproject.toml). They are
not re-exported from this module — import them directly: `from daw.v1 import
engine_pb2_grpc`.
"""

__version__ = "0.1.0"
