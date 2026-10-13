int SharedPumpUsesExplicitFamilyGeometry(){
    // Synthetic second family: different name, node, stroke and diagonal axis.
    // The real physical consumer and palette adapter are exercised together;
    // only the native acknowledgement boundary is the existing mock.
    for(float sign:{-1.f,1.f})for(const auto axis:{std::array<float,3>{.6f,.8f,0},std::array<float,3>{0,0,1}}){
        Fixture f;f.calibration->asset="SyntheticPump";f.sample.asset=f.calibration->asset;
        f.calibration->profileId=0x37500001;f.calibration->geometry={"jntWpn_1","OtherForeEnd",axis,.135f};
        f.calibration->rearDirection=sign;f.rig.names[2]="OtherForeEnd";
        f.calibration->rigFingerprint=bc2_pump_detail::DerivePart(f.rig,f.calibration->geometry)->fingerprint;
        CHECK(PumpCalibrationValid(*f.calibration)&&f.Grip());
        const auto original=f.rig.nativeEvaluated;unsigned palettes=0;
        const float stroke=f.calibration->geometry.stroke;
        const float steps[]{stroke/6,stroke*2/6,stroke*3/6,stroke*4/6,stroke*5/6,stroke,stroke,stroke,stroke,
            stroke*5/6,stroke*4/6,stroke*3/6,stroke*2/6,stroke/6,0,0,0,0,0,0};
        for(float travel:steps){
            const auto result=f.Tick(travel);
            if(result.tracking.target){
                const auto& target=*result.tracking.target;
                CHECK(result.tracking.profile.id==f.calibration->profileId&&Near(result.tracking.profile.stroke,stroke));
                for(float units:{1.f,100.f}){
                    auto placed=Pose(2*units);placed.values[0]={0,0,1,0};placed.values[2]={-1,0,0,0};
                    const auto plan=BuildPumpPresentation(result.tracking,f.rig,placed,units,f.sample.input.nowNs);
                    CHECK(plan.part&&plan.wrist&&plan.fixedAssembly.empty()&&plan.part->index==2);++palettes;
                    const auto relative=Multiply(plan.part->transform,*InverseRigid(placed));
                    for(unsigned n=0;n<3;++n)
                        CHECK(Near(relative.values[3][n]/units,f.calibration->closedPart.values[3][n]+axis[n]*sign*target.travel,.00001f));
                }
            }
            if(f.native.released)break;
        }
        CHECK(palettes>16&&f.native.released==1);f.Tick(0);CHECK(f.native.acked==1&&!f.result.blocksFire);
        CHECK(f.rig.nativeEvaluated==original);
        auto bad=*f.calibration;bad.geometry.axis={0,0,0};CHECK(!PumpCalibrationValid(bad));
        bad=*f.calibration;bad.geometry.stroke=0;CHECK(!PumpCalibrationValid(bad));
        bad=*f.calibration;bad.geometry.part=bad.geometry.root;CHECK(!PumpCalibrationValid(bad));
        bad=*f.calibration;bad.profileId=0;CHECK(!PumpCalibrationValid(bad));
        bad=*f.calibration;bad.asset={};CHECK(!PumpCalibrationValid(bad));
    }
    Fixture wrong;wrong.sample.asset="ForeignPump";wrong.Tick(0,false);wrong.Shot();
    wrong.Tick(0);wrong.Tick(.04f);CHECK(!wrong.result.ownsHand&&wrong.native.released==0);
    return 0;
}
