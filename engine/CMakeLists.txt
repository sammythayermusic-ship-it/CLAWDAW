cmake_minimum_required(VERSION 3.22)
project(clawdaw_engine LANGUAGES CXX C OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Homebrew lays gRPC + protobuf + abseil under /opt/homebrew on Apple Silicon.
list(APPEND CMAKE_PREFIX_PATH /opt/homebrew)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(Threads REQUIRED)

# Generated proto/gRPC sources (engine/generated/) — produced by `buf generate`.
file(GLOB_RECURSE GENERATED_PB_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.pb.cc"
)

add_library(clawdaw_proto STATIC ${GENERATED_PB_SOURCES})
target_include_directories(clawdaw_proto PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
)
target_link_libraries(clawdaw_proto PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc++_reflection
)

# Bring in Tracktion Engine (which transitively pulls JUCE from
# third_party/tracktion_engine/modules/juce — pinned to the commit Tracktion
# expects). EXCLUDE_FROM_ALL keeps Tracktion's example targets out of our
# default `cmake --build` invocation.
add_subdirectory(third_party/tracktion_engine EXCLUDE_FROM_ALL)

add_executable(clawdaw_engine src/main.cc)
target_link_libraries(clawdaw_engine PRIVATE
    clawdaw_proto
    Threads::Threads
    tracktion::tracktion_engine
)

# JUCE_MODAL_LOOPS_PERMITTED gates MessageManager::runDispatchLoopUntil, which
# is the supported way to pump the JUCE message queue from a headless console
# process. Without it, our main loop has no way to drain messages posted by
# RPC handlers acquiring MessageManagerLock — they'd deadlock on first
# mutation. Tracktion's own TestRunner sets this same flag for the same
# reason.
target_compile_definitions(clawdaw_engine PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
)
