#pragma once
#include "SceneDrawPayloadPlan.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
struct ID3D11DeviceContext;
struct ID3D11Buffer;

namespace fvr::debug {
enum class PayloadStatus : unsigned {
    Unseen,Absent,UnsupportedView,InvalidRange,CreateFailed,Pending,Ready,MapFailed,Cancelled,PredicatedNotCopied
};
struct PayloadRecord {
    PayloadRange requested{};PayloadStatus status=PayloadStatus::Unseen;
    std::uint64_t resource=0,view=0;
    unsigned resourceDimension=0,bufferBytes=0,structureStride=0,bindFlags=0,miscFlags=0;
    unsigned srvDimension=0,srvFormat=0,firstElement=0,elements=0,srvFlags=0;
    std::array<std::uint32_t,8> srvDescriptorWords{};unsigned srvDescriptorBytes=0;
    unsigned bindingOffset=0,bindingStride=0,indexFormat=0;
    unsigned copiedOffset=0,copiedBytes=0,payloadOffset=0;
    std::int32_t result=0;
};
struct PayloadEye {
    bool captured=false,finished=false;
    unsigned ordinal=0;std::uint64_t view=0;
    std::uint64_t inputLayout=0,predicate=0;unsigned topology=0;bool predicateValue=false;
    std::array<std::uint64_t,5> shaders{};
    fvr::graphics::SceneWitnessDrawArguments actual{};
    std::array<PayloadRecord,PayloadRangeLimit> records{};
};
struct PayloadResult {
    PayloadSelection selection{};fvr::graphics::SceneWitnessKey key{};
    std::array<PayloadEye,2> eyes{};
    unsigned wrongThread=0;bool identityFailure=false,shapeFailure=false,missingDraw=false;
    // Deliberately no success flag claiming a destroyed object was identified.
    // Input-layout semantics and shader consumption are not inferred from bytes.
    std::array<std::byte,PayloadPairBytes> bytes{};
};
// New isolated helper: NOT linked or called by any normal runtime. Integration
// must be reviewed separately. One instance accepts one exact two-eye pair.
// All methods after construction/destruction run on the immediate-context owner.
// Snapshot is a one-time CPU copy after Poll or Close has terminated all copies;
// a worker can serialize that plain result without retaining any native objects.
class D3D11SceneDrawPayloadWitness {
public:
    explicit D3D11SceneDrawPayloadWitness(const PayloadSelection&);
    ~D3D11SceneDrawPayloadWitness();
    D3D11SceneDrawPayloadWitness(const D3D11SceneDrawPayloadWitness&)=delete;
    D3D11SceneDrawPayloadWitness& operator=(const D3D11SceneDrawPayloadWitness&)=delete;
    bool Valid()const noexcept;
    bool Observe(ID3D11DeviceContext*,const fvr::graphics::SceneWitnessKey&,unsigned eye,
        std::uint64_t view,unsigned ordinal,const fvr::graphics::SceneWitnessDrawArguments&,
        ID3D11Buffer* indirectArguments=nullptr)noexcept;
    bool EndEye(ID3D11DeviceContext*,const fvr::graphics::SceneWitnessKey&,unsigned eye,std::uint64_t view)noexcept;
    void Poll(ID3D11DeviceContext*)noexcept;
    bool Snapshot(ID3D11DeviceContext*,PayloadResult&)noexcept;
    bool CloseOnGraphicsThread(ID3D11DeviceContext*)noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
