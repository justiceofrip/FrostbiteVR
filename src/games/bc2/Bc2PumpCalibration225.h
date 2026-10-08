#pragma once
#include "Bc2PhysicalPump.h"
namespace fvr::bc2 {
// Original native rig observation, not production native-cycle admission.
// Trace SHA256 c712a64becad8b59591c792c891cdb7d8e3fff17cccc2ab29c09f47ff680d03c.
// Actual closed row95/input174: medoid of87 all-three idle loaded8 samples.
// Rear extrema at rows136 and273 both move +0.094635009m along weapon-local Z.
// No default callsite enables this private calibration or the native service.
inline Bc2PumpCalibration MeasuredSpasPump225()noexcept {
    Bc2PumpCalibration c;c.revision=225;c.rigFingerprint=0xa7f219a1426216abull;c.rearDirection=1;c.measured=true;
    c.closedPart={{{{.999999523f,1.21071935e-7f,-2.68220901e-7f,0},
                   {1.21071935e-7f,.999999285f,-2.39815563e-7f,0},
                   {-2.68220901e-7f,-2.39815563e-7f,.999999285f,0},
                   {7.62939453e-6f,-.0553283691f,-.846984863f,1}}}};
    c.closedWrist={{{{.269950688f,.881157637f,-.388184607f,0},
                    {.642518103f,-.465114027f,-.60896492f,0},
                    {-.717144251f,-.0850247592f,-.691717625f,0},
                    {.0363464355f,-.100265503f,-.662780762f,1}}}};
    return c;
}
}
