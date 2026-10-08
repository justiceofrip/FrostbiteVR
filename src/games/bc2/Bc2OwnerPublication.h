#pragma once
#include <cstdint>
namespace fvr::bc2 {
enum class OwnerPublicationStatus:unsigned {Published,Deferred,Rejected,Superseded};
enum class OwnerPublicationReason:unsigned {
 Published,DisabledOrInvalidSource,SourceExpired,OwnerGateBusy,OlderSnapshot,
 RevisionChanged,NativeOwnerChanged,ServerReadFailure,ServerOwnerChanged,ClearBarrier
};
struct OwnerPublicationResult {
 OwnerPublicationStatus status=OwnerPublicationStatus::Rejected;
 OwnerPublicationReason reason=OwnerPublicationReason::DisabledOrInvalidSource;
};
enum class OwnerCopyAdmission:unsigned {Acquired,Deferred,Rejected};
// Failed serialization is not evidence that the native owner changed. It may
// not publish/renew a lease, return an identity, or queue global cancellation.
// A known expired/future source still rejects even when the gate is busy.
constexpr OwnerCopyAdmission AdmitOwnerPublicationCopy(bool held,std::int64_t observed,
 std::int64_t deadline,std::int64_t now,bool knownNativeSourceRejected=false)noexcept {
 if(observed<=0||now<observed||deadline<=observed||deadline-observed>250000000||now>=deadline)
  return OwnerCopyAdmission::Rejected;
 if(knownNativeSourceRejected&&!held)return OwnerCopyAdmission::Rejected;
 return held?OwnerCopyAdmission::Acquired:OwnerCopyAdmission::Deferred;
}
constexpr bool OwnerCopyUnavailableCancels(OwnerCopyAdmission result)noexcept {
 return result==OwnerCopyAdmission::Rejected;
}
} // namespace fvr::bc2
