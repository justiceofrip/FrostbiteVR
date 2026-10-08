#include "Test.h"
#include "fvr/interaction/SightGrasp.h"
#include <limits>
using namespace fvr;using namespace interaction;
namespace {
constexpr float halfPi=1.5707963267948966f;
math::Matrix4 At(float x=0,float y=0,float z=0){
    math::Matrix4 out{};for(unsigned n=0;n<4;++n)out.values[n][n]=1;
    out.values[3][0]=x;out.values[3][1]=y;out.values[3][2]=z;return out;
}
math::Vec3 Point(math::Vec3 p,const math::Matrix4& m){
    return {p.x*m.values[0][0]+p.y*m.values[1][0]+p.z*m.values[2][0]+m.values[3][0],
        p.x*m.values[0][1]+p.y*m.values[1][1]+p.z*m.values[2][1]+m.values[3][1],
        p.x*m.values[0][2]+p.y*m.values[1][2]+p.z*m.values[2][2]+m.values[3][2]};
}
bool Close(math::Vec3 a,math::Vec3 b,float epsilon=.00002f){
    return Near(a.x,b.x,epsilon)&&Near(a.y,b.y,epsilon)&&Near(a.z,b.z,epsilon);
}
bool Close(const math::Matrix4& a,const math::Matrix4& b,float epsilon=.00002f){
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
        if(!Near(a.values[r][c],b.values[r][c],epsilon))return false;
    return true;
}
int HingeGeometry(){
    const math::Vec3 pivot{.03f,.05f,-.58f};
    const auto source=At(pivot.x,pivot.y,pivot.z);
    auto rotated=RotateAboutHinge(source,pivot,{1,0,0},halfPi);CHECK(rotated);
    CHECK(Close(Point({},*rotated),pivot)); // Hinge origin stays fixed.
    CHECK(Close(Point({0,0,-.09f},*rotated),{pivot.x,pivot.y+.09f,pivot.z}));
    auto back=RotateAboutHinge(*rotated,pivot,{1,0,0},-halfPi);
    CHECK(back&&Close(*back,source));
    // A different mechanism origin must rotate around the requested pivot,
    // not around its own origin. The helper also serves a separate front sight.
    const auto offset=At(pivot.x,pivot.y,pivot.z-.1f);
    rotated=RotateAboutHinge(offset,pivot,{1,0,0},halfPi);CHECK(rotated);
    CHECK(Close(Point({},*rotated),{pivot.x,pivot.y+.1f,pivot.z}));
    const auto opposite=RotateAboutHinge(offset,pivot,{-1,0,0},-halfPi);
    CHECK(opposite&&Close(*opposite,*rotated));return 0;
}
int FixedPalmAndCapturedOrientation(){
    const math::Vec3 pivot{.03f,.05f,-.58f},anchor{.05f,.05f,-.67f},palm{.014f,.025f,.07f};
    const auto sight=At(pivot.x,pivot.y,pivot.z);
    const auto rolled=RotateAboutHinge(At(-.02f,-.03f,-.6f),{0,0,0},{0,0,1},.73f);CHECK(rolled);
    const auto initialWrist=*rolled;
    const auto binding=SightGraspBinding::Begin(pivot,{1,0,0},sight,initialWrist,anchor,palm);CHECK(binding);
    for(float angle:{0.f,.1f,.4f,.9f,halfPi}){
        const auto out=binding->Evaluate(angle,SightMode::Primary);CHECK(out&&Near(out->appliedRadians,angle));
        CHECK(Close(Point(palm,out->wrist),out->graspPoint));
        const math::Vec3 expected{anchor.x,pivot.y+.09f*std::sin(angle),pivot.z-.09f*std::cos(angle)};
        CHECK(Close(out->graspPoint,expected)); // Same frame point throughout the arc.
        CHECK(Close(Point({},out->sight),pivot));
        const auto expectedWrist=RotateAboutHinge(initialWrist,pivot,{1,0,0},angle);CHECK(expectedWrist);
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
            CHECK(Near(out->wrist.values[r][c],expectedWrist->values[r][c]));
        CHECK(!Close(Point({},out->wrist),out->graspPoint)); // Palm offset is retained.
    }
    CHECK(Close(*rolled,initialWrist)); // Caller source is never modified.
    // Rendering arbitrary intermediate angles cannot accumulate into a later pose.
    const auto expected=binding->Evaluate(.63f,SightMode::Primary);CHECK(expected);
    for(unsigned n=0;n<1000;++n)CHECK(binding->Evaluate(float(n%157)/100.f,SightMode::Primary));
    const auto repeated=binding->Evaluate(.63f,SightMode::Primary);CHECK(repeated);
    CHECK(Close(repeated->sight,expected->sight)&&Close(repeated->wrist,expected->wrist));
    return 0;
}
int ReverseAndClamps(){
    const math::Vec3 pivot{.03f,.05f,-.58f},anchor{.03f,.05f,-.67f},palm{.01f,.02f,.07f};
    const auto forward=SightGraspBinding::Begin(pivot,{1,0,0},At(pivot.x,pivot.y,pivot.z),
        At(-.02f,.03f,-.6f),anchor,palm);CHECK(forward);
    const auto raised=forward->Evaluate(halfPi,SightMode::Primary);CHECK(raised);
    const auto reverse=SightGraspBinding::Begin(pivot,{1,0,0},raised->sight,raised->wrist,raised->graspPoint,palm);CHECK(reverse);
    for(float angle:{0.f,-.2f,-.8f,-halfPi}){
        const auto closing=reverse->Evaluate(angle,SightMode::Secondary);
        const auto opening=forward->Evaluate(halfPi+angle,SightMode::Primary);CHECK(closing&&opening);
        CHECK(Close(closing->sight,opening->sight)&&Close(closing->wrist,opening->wrist));
        CHECK(Close(Point(palm,closing->wrist),closing->graspPoint));
    }
    const auto primaryWrong=forward->Evaluate(-1,SightMode::Primary);
    const auto primaryOver=forward->Evaluate(7,SightMode::Primary);
    const auto secondaryWrong=reverse->Evaluate(1,SightMode::Secondary);
    const auto secondaryOver=reverse->Evaluate(-7,SightMode::Secondary);
    CHECK(primaryWrong&&Near(primaryWrong->appliedRadians,0));
    CHECK(primaryOver&&Near(primaryOver->appliedRadians,halfPi)&&Close(primaryOver->sight,raised->sight));
    CHECK(secondaryWrong&&Near(secondaryWrong->appliedRadians,0)&&Close(secondaryWrong->sight,raised->sight));
    CHECK(secondaryOver&&Near(secondaryOver->appliedRadians,-halfPi));
    return 0;
}
int WorldComposition(){
    const math::Vec3 pivot{.03f,.05f,-.58f},axis{1,0,0},anchor{.04f,.05f,-.67f},palm{.014f,.025f,.07f};
    const auto sight=At(pivot.x,pivot.y,pivot.z),wrist=At(-.02f,.02f,-.61f);
    const auto binding=SightGraspBinding::Begin(pivot,axis,sight,wrist,anchor,palm);CHECK(binding);
    auto world=RotateAboutHinge(At(),{0,0,0},{0,1,0},.81f);CHECK(world);
    world->values[3][0]=4;world->values[3][1]=2;world->values[3][2]=-3;
    const auto origin=Point({},*world),worldAxisPoint=Point(axis,*world);
    const math::Vec3 worldAxis{worldAxisPoint.x-origin.x,worldAxisPoint.y-origin.y,worldAxisPoint.z-origin.z};
    const auto worldBinding=SightGraspBinding::Begin(Point(pivot,*world),worldAxis,
        Multiply(sight,*world),Multiply(wrist,*world),Point(anchor,*world),palm);CHECK(worldBinding);
    const auto local=binding->Evaluate(.7f,SightMode::Primary);
    const auto placed=worldBinding->Evaluate(.7f,SightMode::Primary);CHECK(local&&placed);
    CHECK(Close(Multiply(local->sight,*world),placed->sight));
    CHECK(Close(Multiply(local->wrist,*world),placed->wrist));
    CHECK(Close(Point(local->graspPoint,*world),placed->graspPoint));
    return 0;
}
int Rejections(){
    const auto source=At();const math::Vec3 pivot{},axis{1,0,0},anchor{0,0,-.09f},palm{.01f,.02f,.07f};
    const auto begin=[&](const math::Matrix4& sight,const math::Matrix4& wrist,math::Vec3 p,math::Vec3 a,math::Vec3 contact,math::Vec3 landmark){
        return SightGraspBinding::Begin(p,a,sight,wrist,contact,landmark);
    };
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    for(const math::Vec3 bad:{math::Vec3{},math::Vec3{2,0,0},math::Vec3{nan,0,0},math::Vec3{inf,0,0}}){
        CHECK(!begin(source,source,pivot,bad,anchor,palm));
        CHECK(!RotateAboutHinge(source,pivot,bad,.5f));
    }
    for(float bad:{nan,inf}){
        CHECK(!begin(source,source,{bad,0,0},axis,anchor,palm));
        CHECK(!begin(source,source,pivot,axis,{bad,0,0},palm));
        CHECK(!begin(source,source,pivot,axis,anchor,{bad,0,0}));
        CHECK(!RotateAboutHinge(source,{bad,0,0},axis,.5f));
        CHECK(!RotateAboutHinge(source,pivot,axis,bad));
    }
    for(unsigned kind=0;kind<6;++kind){
        auto bad=source;
        if(kind==0)bad.values[0][0]=-1; // Reflection.
        if(kind==1)bad.values[0][0]=0; // Singular.
        if(kind==2)bad.values[0][0]=2; // Unsupported scale.
        if(kind==3)bad.values[0][1]=.2f; // Shear outside native tolerance.
        if(kind==4)bad.values[0][3]=.2f; // Perspective.
        if(kind==5)bad.values[3][1]=nan;
        CHECK(!begin(bad,source,pivot,axis,anchor,palm));
        CHECK(!begin(source,bad,pivot,axis,anchor,palm));
        CHECK(!RotateAboutHinge(bad,pivot,axis,.5f));
    }
    auto animated=source;animated.values[0][0]=1.001f;
    const auto binding=begin(animated,animated,pivot,axis,anchor,palm);CHECK(binding);
    CHECK(binding->Evaluate(.7f,SightMode::Primary)); // Small native blend error remains supported.
    CHECK(!binding->Evaluate(nan,SightMode::Primary)&&!binding->Evaluate(inf,SightMode::Primary));
    CHECK(!binding->Evaluate(.5f,static_cast<SightMode>(255)));
    return 0;
}
}
int main(){
    if(HingeGeometry()||FixedPalmAndCapturedOrientation()||ReverseAndClamps()||
       WorldComposition()||Rejections())return 1;
    return 0;
}
