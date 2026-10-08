#include "fvr/math/OpticAperture.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace fvr::math { namespace {
struct D3{double x=0,y=0,z=0;};
D3 Sub(D3 a,D3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
D3 Cross(D3 a,D3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double Dot(D3 a,D3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
D3 V(Vec3 v){return {v.x,v.y,v.z};}
bool Finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
double Distance(AperturePlane p,D3 v){return p.x*v.x+p.y*v.y+p.z*v.z+p.w;}
bool Convex(const OpticAperture& a){
    if(!std::isfinite(a.z)||a.count<3||a.count>a.outline.size())return false;
    double sign=0;
    for(unsigned i=0;i<a.count;++i){
        const auto p=a.outline[i],q=a.outline[(i+1)%a.count];
        if(!std::isfinite(p.x)||!std::isfinite(p.y))return false;
        const double dx=double(q.x)-p.x,dy=double(q.y)-p.y;
        if(dx*dx+dy*dy<1e-14)return false;
        // Every other vertex must lie strictly on the same side of each edge.
        // A local-turn-only check incorrectly admits self-intersecting stars.
        for(unsigned j=0;j<a.count;++j){
            if(j==i||j==(i+1)%a.count)continue;
            const auto r=a.outline[j];
            const double cross=dx*(double(r.y)-p.y)-dy*(double(r.x)-p.x);
            if(!std::isfinite(cross)||std::abs(cross)<1e-14)return false;
            if(sign==0)sign=cross;
            else if((cross>0)!=(sign>0))return false;
        }
    }
    return true;
}
// A triangle clipped by at most 32 planes has at most 35 vertices.
bool Intersects(const std::array<D3,3>& triangle,std::span<const AperturePlane> planes){
    std::array<D3,36> polygon{},next{};unsigned count=3;
    std::copy(triangle.begin(),triangle.end(),polygon.begin());
    for(const auto plane:planes){
        unsigned n=0;
        for(unsigned i=0;i<count;++i){
            const auto a=polygon[i],b=polygon[(i+1)%count];
            const double da=Distance(plane,a),db=Distance(plane,b);
            const bool ina=da>=0,inb=db>=0;
            if(ina)next[n++]=a;
            if(ina!=inb){
                const double t=da/(da-db);
                next[n++]={a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),a.z+t*(b.z-a.z)};
            }
        }
        if(n<3)return false;
        polygon=next;count=n;
    }
    for(unsigned i=1;i+1<count;++i){
        const auto area=Cross(Sub(polygon[i],polygon[0]),Sub(polygon[i+1],polygon[0]));
        if(Dot(area,area)>1e-30)return true;
    }
    return false;
}
}
ApertureResult EvaluateOpticAperture(std::span<const OpticAperture> apertures,
    ReticleBounds reticle,Vec3 eye)noexcept{
    ApertureResult result;
    if(apertures.empty()||apertures.size()>2||!Finite(eye)||!Finite(reticle.minimum)||!Finite(reticle.maximum)||
       reticle.minimum.x>=reticle.maximum.x||reticle.minimum.y>=reticle.maximum.y||
       reticle.minimum.z>reticle.maximum.z)return result;
    for(std::size_t i=0;i<apertures.size();++i){
        if(!Convex(apertures[i])||(i&&apertures[i].z<=apertures[i-1].z)||
           reticle.minimum.z<=apertures[i].z)return result;
    }
    if(eye.z>=apertures.front().z-1e-7f){result.visibility=ApertureVisibility::Hidden;return result;}
    for(const auto& a:apertures){
        D3 centre{0,0,a.z};
        for(unsigned i=0;i<a.count;++i){centre.x+=a.outline[i].x/a.count;centre.y+=a.outline[i].y/a.count;}
        for(unsigned i=0;i<a.count;++i){
            const auto p=a.outline[i],q=a.outline[(i+1)%a.count];
            const D3 point{p.x,p.y,a.z},end{q.x,q.y,a.z};
            auto normal=Cross(Sub(end,point),Sub(V(eye),point));
            double length=std::sqrt(Dot(normal,normal));
            if(!std::isfinite(length)||length<1e-15)return {};
            if(Dot(normal,Sub(centre,point))<0)length=-length;
            normal={normal.x/length,normal.y/length,normal.z/length};
            result.planes[result.planeCount++]={normal.x,normal.y,normal.z,-Dot(normal,point)};
        }
    }
    std::array<D3,8> vertices{};
    for(unsigned i=0;i<8;++i)vertices[i]={i&1?reticle.maximum.x:reticle.minimum.x,
        i&2?reticle.maximum.y:reticle.minimum.y,i&4?reticle.maximum.z:reticle.minimum.z};
    bool allInside=true;
    const auto planes=std::span(result.planes).first(result.planeCount);
    for(const auto v:vertices)for(const auto p:planes)if(Distance(p,v)<0)allInside=false;
    if(allInside){result.visibility=ApertureVisibility::Visible;return result;}
    constexpr unsigned triangles[12][3]={{0,1,3},{0,3,2},{4,6,7},{4,7,5},
        {0,4,5},{0,5,1},{2,3,7},{2,7,6},{0,2,6},{0,6,4},{1,5,7},{1,7,3}};
    for(const auto& t:triangles)if(Intersects({vertices[t[0]],vertices[t[1]],vertices[t[2]]},planes)){
        result.visibility=ApertureVisibility::Partial;return result;
    }
    result.visibility=ApertureVisibility::Hidden;return result;
}
}
