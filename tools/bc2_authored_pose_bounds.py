"""Conservative full-curve bounds for an offline weapon-relative reference.

These bounds describe this repository's decoded curves, not the game's native
interpolation. They neither change the strict static-pose classifier nor admit a
grip, reload, projectile or native object. Callers choose explicit tolerances.
"""
from __future__ import annotations

import math
from bc2_granny_curves import IDENTITY, Curve
from bc2_weapon_animation_pipeline import canonical, identity, inverse_rigid, matrix, multiply


def _length(v):
    return math.hypot(*v)


def _distance(a, b):
    return _length([x-y for x,y in zip(a,b)])


def _curve(curve, dimension, duration):
    if not isinstance(curve, Curve) or curve.dimension != dimension or curve.degree not in (0,1,2):
        raise ValueError('Explicit supported decoded curve required')
    controls, knots = curve.controls, curve.knots
    if not 0 < len(controls) <= 16384 or len(controls) != len(knots):
        raise ValueError('Curve control bound')
    if len(controls) > 1 and curve.degree == 0:
        raise ValueError('Unsupported multi-knot step curve')
    if any(not math.isfinite(t) for t in knots) or knots[0] < 0 or any(a>b for a,b in zip(knots,knots[1:])):
        raise ValueError('Finite ordered knots required')
    if knots[-1] > duration + max(.001,duration*.01):
        raise ValueError('Curve outside clip duration')
    if any(len(c)!=dimension or any(isinstance(x,bool) or not isinstance(x,(int,float)) or not math.isfinite(x) for x in c) for c in controls):
        raise ValueError('Finite typed control vectors required')
    return controls


def _local(curves, duration):
    if len(curves) != 3:
        raise ValueError('Position, orientation and scale curves required')
    p = _curve(curves[0],3,duration)
    q = _curve(curves[1],4,duration)
    s = _curve(curves[2],9,duration)
    # Rigidity must hold across the full curve, including cancelled ancestors.
    if any(tuple(v) != IDENTITY[9] for v in s):
        raise ValueError('Full-curve identity scale required')
    position = max(_distance(v,p[0]) for v in p)
    radius = max(_distance(v,q[0]) for v in q)
    norm = _length(q[0])
    lower, upper = norm-radius, norm+radius
    if not .95 < lower <= upper < 1.05:
        raise ValueError('Quaternion convex hull lacks a valid normalization bound')
    # A nonnegative partition of unity keeps every unnormalized spline value
    # within radius of q0. Normalization is bounded by 2*radius/minimum_norm.
    # Unit quaternion chord d bounds the rotation angle by 4*asin(d/2).
    chord = min(2.,2.*radius/lower)
    angle = min(math.pi,4.*math.asin(min(1.,chord*.5)))
    return dict(reference=matrix(p[0],q[0],s[0]),translation=position,angle=angle,
                constant=all(all(v==c[0] for v in c) for c in (p,q,s)),
                orientation_norm_lower=lower,orientation_norm_upper=upper)


def _branch(chain, locals):
    reference = identity()
    position = angle = 0.
    constant = True
    for name in chain:
        local = locals[name]
        # Child translation is rotated by its parent. The prior rotation error
        # does not change the child's translation length under a rigid parent.
        position += local['translation'] + _length(reference[12:15])*min(2.,local['angle'])
        angle = min(math.pi,angle+local['angle'])
        reference = multiply(reference,local['reference'])
        constant &= local['constant']
    return reference, position, angle, constant


def relative_bounds(clip, skeleton, bone, reference='jntWpn_1'):
    """Bound translation/rotation from the time-zero pose over the entire clip.

    Shared ancestry cancels algebraically. Every non-shared local position is
    bounded by its control hull, and every normalized quaternion by its hull's
    distance from zero. Real scale/shear, missing tracks and antipodal controls
    fail explicitly. Endpoint/interior sampling is not used as a proof.
    """
    names, parents = skeleton.names, skeleton.parents
    if not 0 < len(names) <= 512 or len(set(names)) != len(names) or len(parents) != len(names):
        raise ValueError('Exact bounded skeleton required')
    if any(not isinstance(p,int) or isinstance(p,bool) or p < -1 or p >= i for i,p in enumerate(parents)):
        raise ValueError('Skeleton parent order')
    if bone not in names or reference not in names:
        raise ValueError('Requested bone absent from exact skeleton')
    if not math.isfinite(clip.duration) or not 0 < clip.duration <= 600:
        raise ValueError('Bounded clip duration required')
    by_name = {n:i for i,n in enumerate(names)}
    def chain(name):
        result=[]
        while name is not None:
            result.append(name)
            parent=parents[by_name[name]]
            name=names[parent] if parent>=0 else None
        return result
    left, right = chain(bone), chain(reference)
    common = next((name for name in left if name in right),None)
    required = set(left+right)
    if not required <= clip.tracks.keys():
        raise ValueError('Required authored tracks absent/unsupported')
    locals = {name:_local(clip.tracks[name],clip.duration) for name in required}
    unshared_left = left[:left.index(common)] if common else left
    unshared_right = right[:right.index(common)] if common else right
    a,ea,aa,ca = _branch(unshared_left,locals)
    b,eb,ab,cb = _branch(unshared_right,locals)
    # Relative translation is (p_left-p_right)*inverse(R_right).
    position = ea+eb+_distance(a[12:15],b[12:15])*min(2.,ab)
    angle = min(math.pi,aa+ab)
    if not math.isfinite(position) or position > 1e6:
        raise ValueError('Unbounded relative transform')
    # Evaluate after algebraic cancellation. A distant shared world origin must
    # not introduce subtraction loss into a small weapon-local reference.
    pose = canonical(multiply(a,inverse_rigid(b)))
    # Explicit numerical allowance is separate from the analytical hull bound.
    # It covers the double-precision matrix evaluation used by this tool only.
    linear_slack = 1e-8 * (1.+_length(a[12:15])+_length(b[12:15]))
    angular_slack = 1e-8
    return dict(schema='fvr.bc2.authored_relative_pose_bound',schema_version=1,
                bone=bone,reference_bone=reference,common_ancestor=common,
                unshared_bone_chain=unshared_left,unshared_reference_chain=unshared_right,
                reference_time_seconds=0.,reference_pose=pose,duration_seconds=clip.duration,
                translation_hull_bound_m=position,rotation_hull_bound_rad=angle,
                translation_numerical_allowance_m=linear_slack,rotation_numerical_allowance_rad=angular_slack,
                translation_bound_m=position+linear_slack,rotation_bound_rad=angle+angular_slack,
                unshared_controls_constant=ca and cb,
                method='convex_control_hulls_normalization_bound_and_common_ancestor_cancellation',
                target_runtime_interpolation_verified=False,runtime_admission=False)


def within_tolerance(bound, translation_m, rotation_rad):
    """A caller-selected reference tolerance is not runtime capability admission."""
    if any(isinstance(x,bool) or not isinstance(x,(int,float)) or not math.isfinite(x) or x < 0 for x in (translation_m,rotation_rad)):
        raise ValueError('Explicit finite nonnegative tolerances required')
    if bound.get('schema')!='fvr.bc2.authored_relative_pose_bound' or bound.get('schema_version')!=1 or bound.get('runtime_admission') is not False:
        raise ValueError('Non-admitted relative reference bound required')
    values=[bound.get('translation_bound_m'),bound.get('rotation_bound_rad')]
    if any(isinstance(x,bool) or not isinstance(x,(int,float)) or not math.isfinite(x) or x < 0 for x in values):
        raise ValueError('Invalid reference bound')
    return values[0] <= translation_m and values[1] <= rotation_rad
