#pragma once
// The24 exact retained400 configuration/resource joins; geometry is the
// already-shared396 table. No fuzzy asset matching or per-gun native adapter.
#include "CandidateOriginAdmission388.h"
#include "Bc2MuzzlePartProfiles294.h"
namespace fvr::bc2::smoke402 {
inline constexpr std::string_view Function="objects/weapons/handheld/ul_rgl_hk416/ul_smk_hk416_firing.dbx/3810d31a-fbbf-4b1c-af2c-23d5845774c9";
inline constexpr std::string_view FiringSha="205cac62c3f85ef908ec894454bbad16af6e5ba09ce16391d27ec19a161db4aa";
inline constexpr std::string_view ExplosionGuid="528a3a9e-a964-4912-bc51-98d502af8190";
inline constexpr std::string_view ProjectileGuid="987afacc-972c-40f6-8bb9-3bde8fcf6de6";
struct Tuple {
    std::string_view configuration,configurationSha,tupleSha,sourceIdentity,part;
    unsigned bone;math::Vec3 localExit;
};
inline constexpr std::array<Tuple,24> Tuples{{
    {"Objects/Weapons/Handheld/US_rgl_XM320/US_smk_XM320","7ed99a848aaf3444764ca2d17d3262c95ad81d1612ed35e89612cd172ab9921c","533485d28b2507fef4ff2d9c706e439295d6f03d4db041081f1dda2f80879e6d","be14919490291d36ed5c8a8693413c773262f1b2d00edec653b1bbbfe88b5c9b","jntWpn_12",38,{0.02139488728708927f,0.00014935130190104313f,-0.047988526109193262f}},
    {"Objects/Weapons/Handheld/US_rgl_XM320/US_smk_XM320_Kobra","c46a70ca1d3ebd74cb37189765c13240e2d2eaba6f7c82051a0c3dafa7118978","b4edccc6d02e574b8e21baf3689003d2ff564ca0447373b53668f71cfe872a68","c026642c62c188d9dd988096a170ccd9a3fde7176574283599b895e206e700d2","jntWpn_12",38,{0.02139488728708927f,0.00014935130190104313f,-0.047988526109193262f}},
    {"Objects/Weapons/Handheld/US_rgl_XM320/US_smk_XM320_Scoped","0cbd96c21c15eb892705558f433413d111c4230ea7fc45f1b7609f93711a6baa","c9d6c6d3e8b4a7477e83b79059cedb1b90d2331f9031b9f63a949c27692b1e5a","d0d1cdd2b00db2bb1464e4e0e29efed61174f2b6c4b0f07ae939c0a173adc58f","jntWpn_12",38,{0.02139488728708927f,0.00014935130190104313f,-0.047988526109193262f}},
    {"Objects/Weapons/Handheld/BU_rgl_AN94Abakan/BU_smk_AN94Abakan","198b0d710b4da061756db9280b7236fd9e9f874ff11da19001dc9be4544c63de","e7843c59a27376994072cb62d43358b75733b41968b01eeee6854508a705fe2a","b4d5f946d447c9aeb7eae2d927acd3e034903353e3a36947c971644b903b4f15","jntWpn_1",10,{0.0022582399999999999f,-0.013085009999999999f,-0.79294412999999997f}},
    {"Objects/Weapons/Handheld/BU_rgl_AN94Abakan/BU_smk_AN94Abakan_Kobra","8eb1feaaf1468edda96f816e3e2446e3dd1749c82e00be0b17e8c3ad96f1d53e","a3d12a7ea4c3101081511324a515a33489872eff75b3cb9aed1ad8438dd8fdc6","b3be5dec842d9d8d53116361788f9d88d3772330ba3c3299e63813697a0638ec","jntWpn_1",10,{0.0022582399999999999f,-0.013085009999999999f,-0.79294412999999997f}},
    {"Objects/Weapons/Handheld/BU_rgl_AN94Abakan/BU_smk_AN94Abakan_Scoped","5c647d51d050895e07da77e3d204f1b0009201fc70a3029cdc56aa01dbdb6592","a67387ec4f9505bd46e872f04ae1025c931dfcc43855d498a83df35eb233d5b9","2f24824ebd222f880fd7a829e6e05564345d5e7834e6195a0812d01e85ba4720","jntWpn_1",10,{0.0022582399999999999f,-0.013085009999999999f,-0.79294412999999997f}},
    {"Objects/Weapons/Handheld/RU_rgl_GP30/RU_smk_GP30","039b0201faaac5c598e3ec3764e00b8a6f4cc18e3b8bd7d0163a6b959552a6f0","358fb115fd4f84f772c6d8ebeaa93d365925e8a030a44af2886414ac16e28f63","e8d8d7e8e3290db5f3d6b1f3daf7fddc030902bad0fb697433675d69782f3019","jntWpn_1",10,{0.f,-0.00529051f,-0.80527148000000004f}},
    {"Objects/Weapons/Handheld/RU_rgl_GP30/RU_smk_GP30_Kobra","e5791ae632e8ed2a23c6874573721e2793fb1fac956b164691f829139684feb5","b771b527e21fc90cfe483b5a67682516a69e2cb7696526a8a1e0c3e5909e621f","8d7fa52fe9fc3355e05969d67f945e706cc89b6124be9a9d1339473befe108c8","jntWpn_1",10,{0.f,-0.00529051f,-0.80527148000000004f}},
    {"Objects/Weapons/Handheld/RU_rgl_GP30/RU_smk_GP30_Scoped","1949668552dd91ac8d6900ce4e5f40209da691ac94e468346e121f2339317e7f","5d122bb5df21f201be49b3019c6f46fef97b466b0cacb6b015bf693b9b01c0cc","09bcc0f23957dfa4678a0a721796a5c0eb8a6ddf1b412b260ccc85287ac70530","jntWpn_1",10,{0.f,-0.00529051f,-0.80527148000000004f}},
    {"Objects/Weapons/Handheld/BU_rgl_M16A2/BU_smk_M16A2","de390d20c4055bff4bc60e81bff2f2229e2f70d9fcdf72795694784cda6a8560","ccd59fac8a09c00c5fa702289a1b6edd23dc78b1efc0aba143b16b386bd6d232","973740770f80cf66e6153d647587b2b5d1f16f54367a9ea388e0026f98b5abcc","jntWpn_7",40,{-2e-08f,-0.0062751899999999999f,0.0035808799999999998f}},
    {"Objects/Weapons/Handheld/BU_rgl_M16A2/BU_smk_M16A2_Kobra","14fa34e895d1564e887158de214be2c7b2882e1c2cb4e680c1bd056c231dea85","a9b778dec3497fe142d4c088bf73eeb8e3bad772acc1a0dc4bae420c90cb3e0a","0be0df89b9d343658eafe889f3c37d832ce215688708abe80c0004f90729cb78","jntWpn_7",40,{-2e-08f,-0.0062751899999999999f,0.0035808799999999998f}},
    {"Objects/Weapons/Handheld/BU_rgl_M16A2/BU_smk_M16A2_Scoped","aefc3f080fe48c65f11a78671977c39e5207836c6d2f9af361380842471b306f","26ab90abcd8d1382b62ae01eab2be43cb15bdde0332866b186325722f3258f9a","396068e6a3c083a830462ad2267ee2ff8ac70e0355079ee6472eb1350ed6d896","jntWpn_7",40,{-2e-08f,-0.0062751899999999999f,0.0035808799999999998f}},
    {"Objects/Weapons/Handheld/BU_rgl_M16A2/UPG_smk_M16A2","c9f71e5e15ad9497cf3c043cc436d042f1e6e9f45852660eb7a20cd327c2cd3c","9f10ba631c8c391ff7354d7c536839ebe7031fb4a09fe69f36973460de1c53cb","553a1e0381f4521ed17a757557b87d0af5c5e4686af4b9247212bbfe8c1a01c9","jntWpn_7",40,{-2e-08f,-0.0062751899999999999f,0.0035808799999999998f}},
    {"Objects/Weapons/Handheld/BU_rgl_M16A2/UPG_smk_M16A2_Kobra","5b8a85f846f269b724ea80e57b485c16eded195fb1788e42ef9fdd86c73e81a7","7c8048935c06f9d1d8a9a13c8f8e6d26c1d6d81f626f4b5da1e88fca98bc77d2","71c856eedaca67a43f07f9175511816d88cba6cfe125207c2ff701885ac708da","jntWpn_7",40,{-2e-08f,-0.0062751899999999999f,0.0035808799999999998f}},
    {"Objects/Weapons/Handheld/BU_rgl_M16A2/UPG_smk_M16A2_Scoped","78f69b51067d5ca5b3273810a1162fd084c6b9ded12640161af825ae648b5823","2c08e41353ed3701646a3999dd84f9c8d8b36b516b51182ad9c0291486b86fd9","206151c8e33c6e5b859f3c9b7bea381c1187c5da15ef69468f23119c87b5edba","jntWpn_7",40,{-2e-08f,-0.0062751899999999999f,0.0035808799999999998f}},
    {"Objects/Weapons/Handheld/BU_rgl_F2000/BU_smk_F2000","3d96f36369bb2c8d9e035d9baa9888f33d15a39a316f78fe1c8ac40b3bf7d5f3","1bbcb55b378b494085f34b8fa5cb23b718c1c7613aaf33736f56b5bc5f8260ca","3c4b93917cd8ed4326491cdb8071de5850b3a509ed767bee5baf7e1093ba3b18","jntWpn_5",42,{0.00012301f,6.0999999999999999e-05f,-0.072447289999999998f}},
    {"Objects/Weapons/Handheld/BU_rgl_F2000/BU_smk_F2000_Kobra","fc03f8f39dcd7fb8b00adf32dd51a90fdc5604f7e0d9e1ea4861454b7a94a8bd","b13ad56333c5d186e20e5f0c04f193c93210ff9e1520da19fd16716a83b95b4c","04861e09f7c71b93065d233a1eb834676b36c09389a6a96eee84d2d064ff928d","jntWpn_5",42,{0.00012301f,6.0999999999999999e-05f,-0.072447289999999998f}},
    {"Objects/Weapons/Handheld/BU_rgl_F2000/BU_smk_F2000_Scoped","238e8df044615dec8048570f95528ad796f53875b970f13a02e3b65e39e96655","ff84a83ce8e56067bf5b37613cf6fa28c110a45d82a3c1b1d2033d4af65a72c5","9a41812b8103bdd8297a1d1eef2aded4e1ca2804f585839192f09df885a9f105","jntWpn_5",42,{0.00012301f,6.0999999999999999e-05f,-0.072447289999999998f}},
    {"Objects/Weapons/Handheld/MEC_rgl_M203/MEC_smk_M203","8829e66828a75db20212e35bed7f8f80b41e0d49f780919a26b46b7808633c43","5eed46441125b271d54e3a3ae58b35f13bfa64849bbcda075fa70c34ff89614a","67e927be1ac4eb27ee2c2983b12b01318e6f908111b5134c74a2b9681e33fab5","jntWpn_12",38,{-2.4499999999999999e-05f,-7.0999999999999998e-07f,-0.063991699999999999f}},
    {"Objects/Weapons/Handheld/MEC_rgl_M203/MEC_smk_M203_Kobra","fe4fdd05a735439557720c51dc97f3ea996c88691a0e7f244bc386263a758ac9","088383cfb402956462d3cba3e2ba7599d0a22da82a7ea8d3a13f12dbf63b195b","636e6586e62699cf81443a26a42e0e1f63cbde38db157430ffa3e9f16f7dbd91","jntWpn_12",38,{-2.4499999999999999e-05f,-7.0999999999999998e-07f,-0.063991699999999999f}},
    {"Objects/Weapons/Handheld/MEC_rgl_M203/MEC_smk_M203_Scoped","b4ae9147a49f7129cf8df399fe1ae56037f117c6d022c5ac3b1e92660432fee5","8289983be7c0660cf9e9e3aeb6b077d4a9d1cce1bc8e5c71bcb4546eaac7799b","1b7aa76d58e59d95b9dbe54c0cc45d482f26df558771b4b41b6cf55c5ebd6245","jntWpn_12",38,{-2.4499999999999999e-05f,-7.0999999999999998e-07f,-0.063991699999999999f}},
    {"Objects/Weapons/Handheld/UL_rgl_HK416/UL_smk_HK416","ecda72cad0a2a099e214e8474be97dde0e17cb28a0af9c36b65c5348a56d8893","4d2b1ee8eb28ed7e43466425f11ff001ab20ebaa619cb5438f2830f65ac93742","daf923005e09c340b9429cc23841ae6998086c85ecc2942731b0578d0ee420d3","jntWpn_9",34,{-8.2714499054215104e-08f,-1.8152804848298165e-05f,-0.13286130502820168f}},
    {"Objects/Weapons/Handheld/UL_rgl_HK416/UL_smk_HK416_Kobra","2621cf2b430d9ae12644bf79871df1a99b1d5d4a393f3ba01425c50e75dd0d55","f1a7b9cec88a5cfe89d473c35cf8378ad69ee81ab75d4ce96c3e110be5f2cf7d","bacfca24c28667714c1a7a0d4f3fb3afcfa0e93e8bd880baaac18c7feb288b24","jntWpn_9",34,{-8.2714499054215104e-08f,-1.8152804848298165e-05f,-0.13286130502820168f}},
    {"Objects/Weapons/Handheld/UL_rgl_HK416/UL_smk_HK416_Scope","63c13f7bf67d56d59f0986769d7326b5ab57835b6b07389c3763d3d51ed98da5","80b9628f725ec0766e8dff006154570989042149a99c3061158412fb58e0e33c","3b897f267bf34db5c4ccb63b73ae810059c854322821601effbe22b7230b9622","jntWpn_9",34,{-8.2714499054215104e-08f,-1.8152804848298165e-05f,-0.13286130502820168f}},
}};
struct Enrollment {
    const Tuple* provenance=nullptr;
    origin388::CandidateOriginEnrollment geometry{};
};
constexpr bool ProfileMatches(const Tuple& t,const muzzle_part_observation::Profile& p)noexcept {
    return p.configuration==t.configuration&&p.asset=="40mmsmk"&&p.tupleSha256==t.sourceIdentity&&
        p.part==t.part&&p.expectedIndex==t.bone&&p.state==0&&p.localExitMeters.x==t.localExit.x&&
        p.localExitMeters.y==t.localExit.y&&p.localExitMeters.z==t.localExit.z;
}
inline constexpr origin388::Route SmokeRoute{{0,0,50},{0,0,1},{-0.11999999731779099f,-0.15000000596046448f,0.5f},0,1,1,{1,1,0}};
inline constexpr auto Enrollments=[] {
    std::array<Enrollment,24> out{};
    for(unsigned i=0;i<Tuples.size();++i){
        auto& e=out[i];e.provenance=&Tuples[i];
        for(const auto& p:muzzle_part_observation::Profiles294)if(ProfileMatches(Tuples[i],p)){
            e.geometry={origin388::Admission::Candidate,&p,nullptr,
                "400 exact smoke tuple;396 current selected ordinary state0 rendered part",
                "400 verified57a106 absolute P and57a176 native-selection epoch;7155b0",
                origin388::Boundary::AbsoluteNativeWorldRh,0,SmokeRoute,2.f,(std::numeric_limits<float>::max)()};
        }
    }
    return out;
}();
static_assert([]{for(const auto& e:Enrollments)if(!e.geometry.observedPart)return false;return true;}(),
    "All24 retained396 smoke geometries are required; no partial enrollment");
inline bool ExactEnrollment(const Enrollment& e)noexcept {
    bool exact=false;for(const auto& t:Tuples)if(e.provenance==&t)exact=true;
    return exact&&e.geometry.observedPart&&!e.geometry.authoredFirearm&&
        e.geometry.admission==origin388::Admission::Candidate&&ProfileMatches(*e.provenance,*e.geometry.observedPart)&&
        e.geometry.activeState==0&&e.geometry.maximumDisplacementMetres>0&&e.geometry.maximumDisplacementMetres<=2;
}
inline bool SmokeRouteMatches(const origin388::Route& r,const NativeMuzzleConfig& c)noexcept {
    return c.muzzleExplosion>=0x10000&&c.projectileData>=0x10000&&c.muzzleExplosion!=c.projectileData&&
        c.speed==SmokeRoute.speed&&c.direction==SmokeRoute.direction&&c.position==SmokeRoute.position&&
        c.inheritedSpeed==0&&c.bulletsPerShell==1&&c.bulletsPerShot==1&&c.cameraFlags==SmokeRoute.cameraFlags&&
        r.speed==c.speed&&r.direction==c.direction&&r.position==c.position&&r.inheritedSpeed==c.inheritedSpeed&&
        r.bulletsPerShell==c.bulletsPerShell&&r.bulletsPerShot==c.bulletsPerShot&&r.cameraFlags==c.cameraFlags;
}
// Keep every388 geometry/publication/config/deadline/bounds check intact.
// Only this separately enrolled smoke route accepts the exact reviewed nonnull
// descriptor pair. Host repeats the entire selected-config and native chain;
// pointer nonnullness or a stable address alone is never this provenance proof.
inline std::optional<origin388::Result> PrepareSmoke(const origin388::Request& r,const Enrollment& enrollment,
    origin388::Gate* outcome=nullptr)noexcept {
    using namespace origin388;using namespace origin388::detail;
    if(!ExactEnrollment(enrollment)||r.enrollment!=&enrollment.geometry||
       r.evidence.publication.configuration!=enrollment.provenance->configuration){
        if(outcome)*outcome=Gate::Enrollment;return {};
    }

    const auto reject=[&](Gate g)->std::optional<Result>{if(outcome)*outcome=g;return {};};
    if(!r.capabilityEnabled)return reject(Gate::Disabled);
    if(!r.firstTargetEntry||r.nestedOrAmbiguous)return reject(Gate::Ordering);
    if(!origin388::detail::Key(r.captured)||r.current!=r.captured||r.evidence.proof!=r.captured||r.coordinate.proof!=r.captured)
        return reject(Gate::Identity);
    if(!r.enrollment||r.enrollment->admission!=origin388::Admission::Candidate)return reject(Gate::Enrollment);
    const auto& p=*r.enrollment;const auto& e=r.evidence;const auto& s=e.publication;
    if(bool(p.observedPart)==bool(p.authoredFirearm)||p.activeStateEvidence.empty()||p.activeState==UINT32_MAX)
        return reject(Gate::Enrollment);
    if(!Publication(s,r.captured)||e.rig.soldier!=r.captured.owner.soldier||e.rig.weak!=r.captured.owner.weak||!e.rig.count)
        return reject(Gate::Publication);
    if(!e.firingAllowed||!e.exitVisible||e.activeState!=p.activeState)return reject(Gate::ActiveExit);
    if(r.capturedConfig!=r.currentConfig||!Config(r.currentConfig,r.captured))return reject(Gate::NativeConfiguration);
    if(!SmokeRouteMatches(p.route,r.currentConfig))return reject(Gate::Route);
    if(p.boundary!=origin388::Boundary::AbsoluteNativeWorldRh||p.coordinateBoundaryEvidence.empty()||
       r.coordinate.boundary!=p.boundary||r.coordinate.unitsPerMetre!=s.unitsPerMetre)return reject(Gate::Boundary);
    std::array<double,3> world{};
    if(p.observedPart){
        if(!e.observedPart||e.authoredFirearm||e.observedPart->profile!=p.observedPart||
           e.observedPart->rig!=e.rig||!part::Current(*e.observedPart,s)||
           e.observedPart->bone!=p.observedPart->expectedIndex||p.observedPart->state!=p.activeState||
           !authored_grip_detail::Digest(p.observedPart->tupleSha256))return reject(Gate::Enrollment);
        const auto& f=*e.observedPart;
        if(!interaction::InverseAnimatedTransform(f.trackedPart))return reject(Gate::Geometry);
        const auto local=p.observedPart->localExitMeters;
        const std::array<double,3> scaled{double(local.x)*s.unitsPerMetre,double(local.y)*s.unitsPerMetre,double(local.z)*s.unitsPerMetre};
        for(unsigned c=0;c<3;++c){
            world[c]=f.trackedPart.values[3][c];
            for(unsigned row=0;row<3;++row)world[c]+=scaled[row]*f.trackedPart.values[row][c];
        }
    }else{
        if(!Authored(e,p)||!AuthoredNativeMatches(r.currentConfig,*e.authoredFirearm))return reject(Gate::Enrollment);
        for(unsigned c=0;c<3;++c)world[c]=e.trackedFlash.values[3][c];
    }
    if(!std::isfinite(p.maximumDisplacementMetres)||p.maximumDisplacementMetres<=0||p.maximumDisplacementMetres>2||
       !std::isfinite(p.maximumAbsoluteWorldUnits)||p.maximumAbsoluteWorldUnits<=0)return reject(Gate::Bounds);
    // Both source transforms are already canonical LH WORLD units. Local metres
    // were scaled above; world translation is neither scaled nor added again.
    world[2]=-world[2]; // established ComposeHook LH -> absolute native RH boundary
    Result out{r.captured,r.enrollment,{}};double distanceSquared=0;
    for(unsigned c=0;c<3;++c){
        if(!std::isfinite(world[c])||std::abs(world[c])>p.maximumAbsoluteWorldUnits||
           !std::isfinite(r.coordinate.nativeInputXYZ[c])||
           std::abs(r.coordinate.nativeInputXYZ[c])>p.maximumAbsoluteWorldUnits)return reject(Gate::Bounds);
        out.nativeXYZ[c]=static_cast<float>(world[c]);
        if(!std::isfinite(out.nativeXYZ[c]))return reject(Gate::Bounds);
        const double d=double(out.nativeXYZ[c])-r.coordinate.nativeInputXYZ[c];distanceSquared+=d*d;
    }
    const double bound=double(p.maximumDisplacementMetres)*s.unitsPerMetre;
    if(distanceSquared>bound*bound)return reject(Gate::Bounds);
    if(outcome)*outcome=Gate::Passed;return out;
}
} // namespace fvr::bc2::smoke402
