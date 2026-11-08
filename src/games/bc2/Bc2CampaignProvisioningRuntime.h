#pragma once
#include "Bc2CampaignProvisioning.h"
#include <filesystem>
namespace fvr::bc2::campaignProvisioning {
std::optional<ProcessIdentity> Identify(std::span<const std::byte> executableImage)noexcept;
// NativeProbe owns installation and the worker. All values default to disabled.
bool Install(const ProcessIdentity&,const ReloadStateMemory&,bool explicitRequest=false)noexcept;
bool Start()noexcept;
bool Stop()noexcept;
bool Submit(const Request&)noexcept;
std::optional<Request> SourceRequest()noexcept;
Receipt Snapshot()noexcept;
std::int64_t Now()noexcept;
void ServiceTransport(const std::filesystem::path& directory)noexcept;
}
