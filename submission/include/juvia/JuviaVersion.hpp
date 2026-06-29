#pragma once

namespace Juvia::version {

constexpr const char* kCommitHash     = "6bc3b6aee626dda1fd39cf882d91a83395682967";
constexpr const char* kCommitShort    = "6bc3b6a";
constexpr const char* kDescribe       = "JUVIA260622-6-g6bc3b6a";
constexpr bool        kDirty          = false;
constexpr const char* kBuildTimestamp = "2026-06-29T06:47:03Z";

// Runtime values baked into libjuvia.so itself.  The k* constants above
// are baked into the CONSUMER's binary at its compile time, so comparing
// kCommitHash against runtime_commit_hash() detects a header/library version
// mismatch.
const char* runtime_commit_hash();
const char* runtime_describe();

} // namespace Juvia::version
