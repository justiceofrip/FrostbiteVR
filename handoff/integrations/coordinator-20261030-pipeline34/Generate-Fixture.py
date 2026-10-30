"""Export synthetic rows through the actual tool for an isolated C++ consumer check."""
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(root / 'tools'))
import bc2_mechanism_registry_header as exporter
from test_bc2_mechanism_registry_header import fixture, admit

out = root / 'reports/coordinator-20261030-pipeline34/generated-fixture'
out.mkdir(parents=True, exist_ok=True)
receipts = {}
for kind, name in [('feed', 'Feed.generated.h'), ('pistol_slide', 'Slide.generated.h')]:
    args = fixture(kind)
    admission = admit(args)
    args[2]['profiles'][0]['enabled'] = True
    header, receipt = exporter.build(*args, admission, header_kind=kind)
    assert receipt['valid'] and receipt['summary']['enabled_rows'] == 1
    (out / name).write_text(header, encoding='utf-8')
    receipts[kind] = receipt
(out / 'receipts.json').write_text(json.dumps(receipts, indent=2), encoding='utf-8')
(out / 'consumer.cpp').write_text(r'''
#include "Bc2FeedEquipmentProfile.h"
#include "Bc2PistolSlideProfile.h"
#include "Test.h"
#include <cstdio>
int main(){
    using namespace fvr::bc2;
    const auto feeds=RegisteredBc2FeedProfiles();
    const auto slides=RegisteredBc2PistolSlideProfiles();
    CHECK(feeds.size()==2&&slides.size()==2);
    const auto* feed=FindBc2FeedRegistration(feeds[1].key);
    const auto* slide=ResolveBc2PistolSlideRegistration(slides[1].key);
    CHECK(feed==&feeds[1]);
    CHECK(slide==&slides[1]);
    CHECK(feed->geometry.asset=="SyntheticFeed"&&feed->config.values.baseCapacity==2);
    CHECK(slide->geometry->selection.asset=="SyntheticSlide"&&slide->configuration.values.baseCapacity==2);
    CHECK(FindBc2FeedRegistration(feed->geometry.asset,feed->geometry.configurationPath,
        feed->geometry.meshPath,feed->geometry.rigFingerprint)==feed);
    CHECK(FindBc2PistolSlideRegistration(slide->geometry->selection)==slide);
    CHECK(BuildBc2FeedInteractionProfile(feed->geometry,feed->mechanism.id,
        feed->mechanism.revision,feed->interactionPolicy).profileId==feed->mechanism.id);
    CHECK(BuildBc2PistolSlideProfile(*slide));
    CHECK(FindBc2FeedRegistration(feeds[0].key)==&feeds[0]);
    CHECK(ResolveBc2PistolSlideRegistration(slides[0].key)==&slides[0]);
    CHECK(!FindBc2FeedRegistration(slide->key)&&!ResolveBc2PistolSlideRegistration(feed->key));
    std::puts("Generated feed and slide headers accepted by actual immutable registry consumers.");
}
''', encoding='utf-8')
print(out)
