#pragma once

namespace Juvia::version {

constexpr const char* kCommitHash     = "654819303de30605f939078cda085d0aaae16f9d";
constexpr const char* kCommitShort    = "6548193";
constexpr const char* kDescribe       = "JUVIA260622-4-g6548193";
constexpr bool        kDirty          = false;
constexpr const char* kBuildTimestamp = "2026-06-26T00:43:22Z";

// Runtime values baked into libjuvia.so itself.  The k* constants above
// are baked into the CONSUMER's binary at its compile time, so comparing
// kCommitHash against runtime_commit_hash() detects a header/library version
// mismatch.
const char* runtime_commit_hash();
const char* runtime_describe();

} // namespace Juvia::version
