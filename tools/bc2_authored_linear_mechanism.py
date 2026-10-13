"""Offline rail measurements from exact authored clips; no native admission.

Extrema apply to bc2_granny_curves' decoded polynomial spline, not to an
unverified native interpolator. A moving leaf is explicitly selected by the
caller; linear movement alone does not identify a pump or charging handle.
"""
from functools import lru_cache
import bisect
import math
from types import SimpleNamespace
from bc2_authored_pose_bounds import _curve, relative_bounds, within_tolerance
from bc2_granny_curves import Curve, IDENTITY


def _cancelled_parent_valid(clip,skeleton,root):
    """Prove common ancestors remain rigid without bounding their movement.

    Adjacent spline controls form a local convex hull. Positive projection on
    one active quaternion bounds its norm away from zero over that knot span;
    the largest control norm bounds it above. No pose samples certify rigidity.
    """
    index=skeleton.names.index(root);seen=set()
    while index!=-1:
        if not 0<=index<len(skeleton.names) or index in seen:raise ValueError('Invalid ancestor hierarchy')
        seen.add(index);name=skeleton.names[index]
        if name not in clip.tracks:raise ValueError('Missing ancestor tracks')
        p,q,s=clip.tracks[name];_curve(p,3,clip.duration)
        controls=_curve(q,4,clip.duration);scales=_curve(s,9,clip.duration)
        if any(tuple(v)!=IDENTITY[9] for v in scales):raise ValueError('Common ancestor scale is not identity')
        if any(not .95<math.hypot(*v)<1.05 for v in controls):raise ValueError('Invalid ancestor orientation')
        n=len(controls)
        for left,right in zip(sorted(set(q.knots)),sorted(set(q.knots))[1:]):
            if left>=clip.duration:continue
            span=bisect.bisect_right(q.knots,(left+min(right,clip.duration))/2)-1
            active=[controls[max(0,min(n-1,i+1))] for i in range(span-q.degree,span+1)]
            center=active[len(active)//2];norm=math.hypot(*center)
            if min(sum(x*y for x,y in zip(center,v))/norm for v in active)<=.95:
                raise ValueError('Common ancestor quaternion hull crosses decoder normalization boundary')
        index=skeleton.parents[index]


def _direct_bound(clip,skeleton,part,root):
    _cancelled_parent_valid(clip,skeleton,root)
    identity_tracks=tuple(Curve('identity',0,d,(0.,),(IDENTITY[d],)) for d in (3,4,9))
    local=SimpleNamespace(duration=clip.duration,tracks={root:identity_tracks,part:clip.tracks[part]})
    return relative_bounds(local,SimpleNamespace(names=[root,part],parents=[-1,0]),part,root)


def projection_extrema(curve, axis, origin, duration):
    if not isinstance(duration,(int,float)) or isinstance(duration,bool) or not math.isfinite(duration) or not 0<duration<=600:
        raise ValueError('Bounded positive duration required')
    controls = _curve(curve, 3, duration)
    if len(axis)!=3 or len(origin)!=3 or not all(math.isfinite(x) for x in (*axis,*origin)):
        raise ValueError('Finite projection required')
    if abs(math.hypot(*axis)-1)>1e-9:
        raise ValueError('Unit projection axis required')
    values=[sum((p[k]-origin[k])*axis[k] for k in range(3)) for p in controls]
    n=len(values)
    candidates=[(t,sum((p[k]-origin[k])*axis[k] for k in range(3))) for t,p in ((0.,curve.evaluate(0.)),(duration,curve.evaluate(duration)))]
    def knot(i):return curve.knots[max(0,min(n-1,i))]
    def linear_product(poly,b,a):
        out=[0.]*(len(poly)+1)
        for i,x in enumerate(poly):out[i]+=b*x;out[i+1]+=a*x
        return out
    def add(a,b):return [(a[i] if i<len(a) else 0)+(b[i] if i<len(b) else 0) for i in range(max(len(a),len(b)))]
    for left,right in zip(sorted(set(curve.knots)),sorted(set(curve.knots))[1:]):
        left=max(0.,left);right=min(duration,right)
        if right<=left:continue
        width=right-left;mid=(left+right)/2
        span=bisect.bisect_right(curve.knots,mid)-1
        @lru_cache(None)
        def basis(i,degree):
            if degree==0:return (float(knot(i)<=mid<knot(i+1)),)
            a=knot(i+degree)-knot(i);b=knot(i+degree+1)-knot(i+1)
            p=linear_product(basis(i,degree-1),(left-knot(i))/a,width/a) if a else [0.]
            q=linear_product(basis(i+1,degree-1),(knot(i+degree+1)-left)/b,-width/b) if b else [0.]
            return tuple(add(p,q))
        polynomial=[0.,0.,0.]
        for i in range(span-curve.degree,span+1):
            for j,w in enumerate(basis(i,curve.degree)):polynomial[j]+=w*values[max(0,min(n-1,i+1))]
        c,b,a=polynomial;points=[0.,1.]
        if a and 0< -b/(2*a)<1:points.append(-b/(2*a))
        for u in points:candidates.append((left+width*u,c+b*u+a*u*u))
    if any(not math.isfinite(v) for _,v in candidates):raise ValueError('Unbounded projection')
    low=min(candidates,key=lambda v:v[1]);high=max(candidates,key=lambda v:v[1])
    return dict(minimum_m=low[1],maximum_m=high[1],minimum_time_seconds=low[0],maximum_time_seconds=high[0],
                method='per_knot_polynomial_endpoints_and_stationary_points',includes_one_sided_knot_limits=True,runtime_interpolation_verified=False)


def measure(clip,reference_clip,skeleton,part,root='jntWpn_1',*,lateral_tolerance_m=.001,rotation_tolerance_rad=.001):
    names,parents=skeleton.names,skeleton.parents
    if not 0<len(names)<=512 or len(parents)!=len(names) or len(set(names))!=len(names):
        raise ValueError('Exact bounded skeleton required')
    if any(not isinstance(p,int) or isinstance(p,bool) or p < -1 or p >= i for i,p in enumerate(parents)):
        raise ValueError('Skeleton parent order')
    for source in (clip,reference_clip):
        if not isinstance(source.duration,(int,float)) or isinstance(source.duration,bool) or not math.isfinite(source.duration) or not 0<source.duration<=600:
            raise ValueError('Bounded positive clip duration required')
    if not all(math.isfinite(v) and 0<=v<=.01 for v in (lateral_tolerance_m,rotation_tolerance_rad)):
        raise ValueError('Explicit small finite tolerances required')
    if part not in skeleton.names or root not in skeleton.names:raise ValueError('Missing named part')
    index=skeleton.names.index(part);parent=skeleton.parents[index]
    if parent!=skeleton.names.index(root) or index in skeleton.parents:raise ValueError('Exact direct-child leaf required')
    # This also validates all ancestor tracks and scale, even though common
    # ancestor motion cancels from the part's weapon-relative placement.
    if part not in clip.tracks or part not in reference_clip.tracks:raise ValueError('Missing part tracks')
    closed=_direct_bound(reference_clip,skeleton,part,root)
    motion=_direct_bound(clip,skeleton,part,root)
    if not within_tolerance(closed,.00025,rotation_tolerance_rad):raise ValueError('Reference pose moves')
    matrix=closed['reference_pose'];initial=motion['reference_pose']
    # Row-vector relative orientation; trace(Rclosed * Rinitial^T).
    cosine=(sum(matrix[i*4+j]*initial[i*4+j] for i in range(3) for j in range(3))-1)/2
    rotation=math.acos(max(-1.,min(1.,cosine)))+motion['rotation_bound_rad']+closed['rotation_bound_rad']
    if rotation>rotation_tolerance_rad:raise ValueError('Part is not a translation-only rail')
    curve=clip.tracks[part][0];controls=_curve(curve,3,clip.duration)
    # Direct child position is already weapon-relative; reflect archive Z once.
    points=[(p[0],p[1],-p[2]) for p in controls];origin=matrix[12:15]
    far=max(points,key=lambda p:math.dist(p,origin));length=math.dist(far,origin)
    if length<.01 or length>.5:raise ValueError('No bounded mechanical stroke')
    axis=tuple((far[k]-origin[k])/length for k in range(3))
    lateral=max(math.hypot(*(p[k]-origin[k]-sum((p[j]-origin[j])*axis[j] for j in range(3))*axis[k] for k in range(3))) for p in points)
    if lateral+closed['translation_bound_m']>lateral_tolerance_m:raise ValueError('Nonlinear control hull')
    # Projection in raw archive coordinates is algebraically equivalent to
    # the reflected-Z canonical axis/origin used by the runtime adapters.
    extrema=projection_extrema(curve,(axis[0],axis[1],-axis[2]),(origin[0],origin[1],-origin[2]),clip.duration)
    if not .01<=extrema['maximum_m']-extrema['minimum_m']<=.5:
        raise ValueError('Decoded range is not a bounded mechanical stroke')
    return dict(schema='fvr.bc2.authored_linear_mechanism.v1',part=part,root=root,axis=list(axis),
                reference_part=matrix,reference_pose_bound=closed,lateral_control_hull_bound_m=lateral,
                reference_is_minimum_endpoint=extrema['minimum_m']>=-lateral_tolerance_m,
                runtime_closed_boundary_verified=False,
                rotation_bound_rad=rotation,decoded_travel=extrema,
                clip_sha256=clip.sha256,reference_clip_sha256=reference_clip.sha256,
                skeleton_sha256=skeleton.sha256,rig_fingerprint=skeleton.fingerprint,
                mechanism_kind='unassigned',native_shot_or_chamber_semantics_verified=False,
                native_admission=False,runtime_enabled=False)


def main():
    import argparse
    import json
    from pathlib import Path
    from bc2_weapon_animation_pipeline import Clip,Skeleton
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clip',type=Path,required=True)
    parser.add_argument('--reference-clip',type=Path,required=True)
    parser.add_argument('--skeleton',type=Path,required=True)
    parser.add_argument('--part',required=True)
    parser.add_argument('--root',default='jntWpn_1')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=measure(Clip(args.clip.read_bytes()),Clip(args.reference_clip.read_bytes()),
                   Skeleton(args.skeleton.read_bytes()),args.part,args.root)
    # A completed report is evidence; require a fresh destination on every run.
    with args.output.open('x',encoding='utf-8') as output:
        output.write(json.dumps(result,indent=2,allow_nan=False)+'\n')


if __name__=='__main__':main()
