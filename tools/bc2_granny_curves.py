"""Supported authored curve decoding and bounded spline evaluation.

Quantized formats follow Norbyte/LSLib's MIT-licensed format implementation;
see THIRD-PARTY-NOTICES.md. No runtime DLL or original game asset is shipped.
"""
from dataclasses import dataclass
import bisect
import math
import struct

IDENTITY={3:(0.,0.,0.),4:(0.,0.,0.,1.),9:(1.,0.,0.,0.,1.,0.,0.,0.,1.)}
FORMATS={'DaK32fC32f':1,'DaIdentity':2,'D3Constant32f':4,'D4Constant32f':5,
         'D4nK16uC15u':8,'D4nK8uC7u':9,'D3K16uC16u':10,'D3K8uC8u':11,'D3I1K8uC8u':18}
SCALE=(1.4142135,.70710677,.35355338,.35355338,.35355338,.17677669,.17677669,.17677669)
OFFSET=(-.70710677,-.35355338,-.53033006,-.17677669,.17677669,-.17677669,-.088388346,0.)

def numbers(value):
    return [next(v for k,v in x.items() if not k.startswith('_')) if isinstance(x,dict) else x for x in value]

def finite(values):
    if not all(isinstance(v,(float,int)) and not isinstance(v,bool) and math.isfinite(v) for v in values):raise ValueError('Nonfinite curve values')

def unit(q):
    length=math.sqrt(sum(v*v for v in q))
    if not .95<length<1.05:raise ValueError('Non-unit authored orientation')
    return tuple(v/length for v in q)

@dataclass(frozen=True)
class Curve:
    format:str
    degree:int
    dimension:int
    knots:tuple
    controls:tuple
    def evaluate(self,time):
        if not math.isfinite(time):raise ValueError('Nonfinite evaluation time')
        n=len(self.controls)
        if n==1 or time<=self.knots[0]:result=self.controls[0]
        elif time>=self.knots[-1]:result=self.controls[-1]
        else:
            # Compact Granny knots have one control per knot. Extend endpoints
            # by clamping; use ordinary Cox-de Boor basis with control index+1.
            span=bisect.bisect_right(self.knots,time)-1;degree=self.degree
            def knot(i):return self.knots[max(0,min(n-1,i))]
            def basis(i,p):
                if p==0:return float(knot(i)<=time<knot(i+1))
                left=knot(i+p)-knot(i);right=knot(i+p+1)-knot(i+1)
                return ((time-knot(i))/left*basis(i,p-1) if left else 0.)+((knot(i+p+1)-time)/right*basis(i+1,p-1) if right else 0.)
            weights=[(max(0,min(n-1,i+1)),basis(i,degree)) for i in range(span-degree,span+1)]
            if abs(sum(w for _,w in weights)-1)>1e-7:raise ValueError('Spline basis partition')
            result=tuple(sum(self.controls[i][c]*w for i,w in weights) for c in range(self.dimension))
        return unit(result) if self.dimension==4 else tuple(result)

def decode(curve,dimension):
    data=curve['CurveData'];headers=[k for k in data if k.startswith('CurveDataHeader_')]
    if len(headers)!=1 or dimension not in IDENTITY:raise ValueError('Curve header or dimension')
    key=headers[0];form=key[16:];header=data[key];degree=header['Degree']
    if form not in FORMATS or header['Format']!=FORMATS[form]:raise ValueError('Unsupported/mismatched curve format '+form)
    if degree not in (0,1,2):raise ValueError('Unsupported spline degree')
    if form=='DaIdentity':
        if data['Dimension']!=dimension or degree:raise ValueError('Identity curve dimension/degree')
        knots=[0.];controls=[IDENTITY[dimension]]
    elif form in ('D3Constant32f','D4Constant32f'):
        if dimension!=int(form[1]) or degree:raise ValueError('Constant curve dimension/degree')
        controls=[tuple(data['Controls'])];knots=[0.]
    elif form=='DaK32fC32f':
        knots=numbers(data['Knots']);packed=numbers(data['Controls'])
        if len(packed)!=len(knots)*dimension:raise ValueError('Float curve count')
        controls=[tuple(packed[n:n+dimension]) for n in range(0,len(packed),dimension)]
    else:
        packed=numbers(data['KnotsControls']);component_count=1 if form=='D3I1K8uC8u' else 3
        if not packed or len(packed)%(component_count+1):raise ValueError('Quantized curve count')
        n=len(packed)//(component_count+1)
        bits=16 if '16u' in form else 8
        if any(not isinstance(v,int) or isinstance(v,bool) or not 0<=v<(1<<bits) for v in packed):raise ValueError('Packed component range')
        scale=data.get('OneOverKnotScale')
        if scale is None:scale=struct.unpack('<f',struct.pack('<I',data['OneOverKnotScaleTrunc']<<16))[0]
        if not math.isfinite(scale) or scale<=0:raise ValueError('Knot scale')
        knots=[v/scale for v in packed[:n]];raw=packed[n:];controls=[]
        if form.startswith('D4n'):
            if dimension!=4:raise ValueError('Quaternion curve dimension')
            signbit=1<<(bits-1);mask=signbit-1;selector=data['ScaleOffsetTableEntries']
            if not 0<=selector<=65535:raise ValueError('Quaternion selector')
            for at in range(0,len(raw),3):
                triplet=raw[at:at+3];omitted=((triplet[1]>>(bits-1))<<1)|(triplet[2]>>(bits-1));q=[0.]*4
                for v,axis in zip(triplet,((omitted+1)%4,(omitted+2)%4,(omitted+3)%4)):
                    code=(selector>>(4*axis))&15;direction=-1 if code>=8 else 1
                    q[axis]=direction*((v&mask)*SCALE[code%8]/mask+OFFSET[code%8])
                remainder=1-sum(v*v for v in q)
                if remainder< -1e-6:raise ValueError('Invalid packed quaternion')
                q[omitted]=math.sqrt(max(0.,remainder))*(-1 if triplet[0]&signbit else 1)
                controls.append(tuple(q))
        else:
            if dimension!=3:raise ValueError('Vector curve dimension')
            scales=data['ControlScales'];offsets=data['ControlOffsets'];finite(scales+offsets)
            if len(scales)!=3 or len(offsets)!=3:raise ValueError('Vector coefficients')
            for at in range(0,len(raw),component_count):
                controls.append(tuple(raw[at+(axis if component_count==3 else 0)]*scales[axis]+offsets[axis] for axis in range(3)))
    if not 0<len(controls)<=16384 or len(knots)!=len(controls):raise ValueError('Curve control bound')
    finite(knots)
    # Repeated knots are valid (including 8-bit endpoint quantization).
    if any(a>b for a,b in zip(knots,knots[1:])) or knots[0]<0:raise ValueError('Knot ordering')
    for c in controls:
        if len(c)!=dimension:raise ValueError('Control dimension')
        finite(c)
        if dimension==4:unit(c)
    if len(controls)>1 and degree==0:raise ValueError('Unsupported multi-knot step curve')
    return Curve(form,degree,dimension,tuple(knots),tuple(controls))
