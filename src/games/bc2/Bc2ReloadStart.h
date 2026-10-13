#pragma once
namespace fvr::bc2 {
// Deferred means no native operation was admitted. It permits retaining physical
// custody while a NEW current input/native cohort retries, never a reload pulse.
enum class ReloadStartResult:unsigned {Accepted,Deferred,Rejected};
}
