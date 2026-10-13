"""Conservative affine texture-coordinate tracing of disassembled DXBC.

Only straight-line mov/add/mul/mad arithmetic is interpreted. Unknown values
remain unknown, including sampled pixels, constants supplied at runtime, and
nonlinear instructions. This is not shader execution or material conversion.
The interface comes from D3DDisassemble's signatures, not guessed registers.
"""
import math
import re

COMPONENTS = 'xyzw'
FLOW = {'if', 'else', 'endif', 'loop', 'endloop', 'switch', 'endswitch', 'case',
        'default', 'break', 'breakc', 'continue', 'continuec', 'call', 'callc',
        'label', 'retc', 'interface_call'}
REGISTER = re.compile(r'([rov]\d+)(?:\.([xyzw]{1,4}))?$')


def _interface(text, kind):
    start = '// ' + kind + ' signature:'
    if text.count(start) != 1:
        raise ValueError('Missing or ambiguous shader interface')
    section = text.split(start, 1)[1]
    section = section.split('// Output signature:', 1)[0] if kind == 'Input' else section
    result = []
    for line in section.splitlines():
        match = re.fullmatch(r'//\s+(\w+)\s+(\d+)\s+([xyzw]+)\s+(\d+)\s+(\w+)\s+(\w+)(?:\s+([xyzw]+))?\s*', line)
        if match:
            name, index, mask, register, system, fmt, _ = match.groups()
            result.append(dict(semantic=name.upper(), index=int(index), mask=mask,
                               register=int(register), system=system, format=fmt))
        elif re.match(r'(?:vs|ps)_\d_\d', line):
            break
    if len(result) > 64 or len({(r['semantic'], r['index']) for r in result}) != len(result):
        raise ValueError('Invalid shader interface')
    return result


def _constant(value):
    return dict(constant=value, terms={}) if math.isfinite(value) else None


def _add(a, b):
    if a is None or b is None:return None
    terms = dict(a['terms'])
    for key, value in b['terms'].items():terms[key] = terms.get(key, 0.) + value
    result = dict(constant=a['constant'] + b['constant'], terms={k:v for k,v in terms.items() if v})
    return result if all(math.isfinite(v) for v in (result['constant'], *result['terms'].values())) else None


def _mul(a, b):
    if a is None or b is None or (a['terms'] and b['terms']):return None
    if a['terms']:a,b=b,a
    scale=a['constant']
    result=dict(constant=scale*b['constant'], terms={k:scale*v for k,v in b['terms'].items() if scale*v})
    return result if all(math.isfinite(v) for v in (result['constant'], *result['terms'].values())) else None


def _arguments(text):
    result=[];begin=depth=0
    for i,ch in enumerate(text):
        if ch=='(':depth+=1
        if ch==')':depth-=1
        if depth<0:raise ValueError('Unbalanced shader operand')
        if ch==',' and depth==0:result.append(text[begin:i].strip());begin=i+1
    if depth:raise ValueError('Unbalanced shader operand')
    result.append(text[begin:].strip())
    return result


def trace_coordinates(text, input_values=None):
    """Track affine expressions and 2D samples; preserve unknown dependencies.

    input_values maps (semantic, index) to four expressions from a preceding
    vertex shader. When absent, each non-system float input is a distinct atom.
    Control flow rejects rather than interpreting both paths as straight-line.
    """
    if not isinstance(text,str) or not 1<=len(text)<=2*1024*1024:
        raise ValueError('Disassembly size')
    inputs=_interface(text,'Input');outputs=_interface(text,'Output')
    lines=[line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith('//')]
    if not lines or lines[0] not in ('vs_4_0','vs_4_1','vs_5_0','ps_4_0','ps_4_1','ps_5_0') or len(lines)>10000:
        raise ValueError('Unsupported shader model or instruction count')
    registers={};samples=[];resources={};terminated=False
    for row in inputs:
        values=list(registers.get('v'+str(row['register']),[None]*4))
        if row['system']=='NONE' and row['format']=='float':
            key=(row['semantic'],row['index'])
            for c in row['mask']:
                if input_values is None:values[COMPONENTS.index(c)]=dict(constant=0.,terms={f'{key[0]}{key[1]}.{c}':1.})
                elif key in input_values:values[COMPONENTS.index(c)]=input_values[key][COMPONENTS.index(c)]
        registers['v'+str(row['register'])]=values

    def operand(value):
        sign=1.
        if value.startswith('-'):sign=-1.;value=value[1:]
        match=REGISTER.fullmatch(value)
        if match:
            name,swizzle=match.groups();swizzle=swizzle or 'xyzw'
            if len(swizzle)==1:swizzle*=4
            if len(swizzle)!=4:return [None]*4
            result=[registers.get(name,[None]*4)[COMPONENTS.index(c)] for c in swizzle]
        elif value.startswith('l(') and value.endswith(')'):
            try:
                raw=[float(x.strip()) for x in value[2:-1].split(',')]
                if len(raw)==1:raw*=4
                result=[_constant(x) for x in raw] if len(raw)==4 else [None]*4
            except ValueError:result=[None]*4
        else:result=[None]*4
        return result if sign==1. else [_mul(_constant(-1.),x) for x in result]

    def assign(destination,values):
        if destination=='null':return
        match=REGISTER.fullmatch(destination)
        if not match or match[1].startswith('v'):raise ValueError('Unsupported instruction destination')
        name,mask=match.groups();mask=mask or 'xyzw'
        if len(set(mask))!=len(mask):raise ValueError('Duplicate destination component')
        result=list(registers.get(name,[None]*4))
        for c in mask:result[COMPONENTS.index(c)]=values[COMPONENTS.index(c)]
        registers[name]=result

    for line in lines[1:]:
        if terminated:raise ValueError('Instructions after shader return')
        op,_,tail=line.partition(' ')
        if op.split('_',1)[0] in FLOW:raise ValueError('Unsupported shader control flow: '+op)
        if op=='dcl_resource_texture2d':
            reg=re.search(r'\bt(\d+)\b',tail)
            if reg:resources[int(reg[1])]='2d'
        if op.startswith('dcl_'):continue
        if op=='ret':terminated=True;continue
        args=_arguments(tail)
        if op.startswith('sample'):
            if len(args)<4:raise ValueError('Incomplete sample instruction')
            texture=re.fullmatch(r't(\d+)(?:\.[xyzw]+)?',args[2])
            if texture and resources.get(int(texture[1]))=='2d':
                uv=operand(args[1])[:2]
                samples.append(dict(texture_slot=int(texture[1]),coordinates=uv,
                                    affine=all(v is not None for v in uv),instruction=op))
            assign(args[0],[None]*4);continue
        if op in ('mov','add','mul','mad'):
            needed={'mov':2,'add':3,'mul':3,'mad':4}[op]
            if len(args)!=needed:raise ValueError('Arithmetic operand count')
            values=[operand(a) for a in args[1:]]
            result=[]
            for i in range(4):
                a=values[0][i]
                result.append(a if op=='mov' else _add(a,values[1][i]) if op=='add' else
                              _mul(a,values[1][i]) if op=='mul' else _add(_mul(a,values[1][i]),values[2][i]))
            assign(args[0],result)
        elif op in ('imul','umul','udiv','sincos','swapc'):
            if len(args)<3:raise ValueError('Incomplete multi-destination instruction')
            assign(args[0],[None]*4);assign(args[1],[None]*4)
        elif op.split('_',1)[0] in ('discard','nop'):
            continue
        else:
            # Nonlinear/runtime operations invalidate their written components.
            assign(args[0],[None]*4)
    if not terminated:raise ValueError('Missing shader return')
    mapped={}
    for row in outputs:
        values=registers.get('o'+str(row['register']),[None]*4)
        mapped[(row['semantic'],row['index'])]=[values[i] if c in row['mask'] else None for i,c in enumerate(COMPONENTS)]
    return dict(model=lines[0],outputs=mapped,samples=samples)


def evaluate_coordinates(coordinates, channels, vertex_count):
    """Apply traced UV arithmetic to decoded channels, without clamping/flips."""
    if type(vertex_count) is not int or not 1 <= vertex_count <= 65536:
        raise ValueError('Coordinate vertex count')
    if not isinstance(coordinates, (list,tuple)) or len(coordinates)!=2:
        raise ValueError('Two texture coordinate expressions required')
    result=[[0.,0.] for _ in range(vertex_count)]
    for axis,expression in enumerate(coordinates):
        if not isinstance(expression,dict) or not math.isfinite(expression['constant']):
            raise ValueError('Unknown texture coordinate')
        for row in result:row[axis]=expression['constant']
        if not isinstance(expression['terms'],dict) or len(expression['terms'])>64:
            raise ValueError('Texture coordinate term bound')
        for atom,coefficient in expression['terms'].items():
            match=re.fullmatch(r'([A-Z_]+)(\d+)\.([xyzw])',atom)
            if not match or not math.isfinite(coefficient):raise ValueError('Texture coordinate atom')
            semantic,index,component=match.groups();channel=channels.get((semantic,int(index)))
            if channel is None or channel['count']!=vertex_count or len(channel['values'])!=vertex_count:
                raise ValueError('Missing or mismatched decoded channel')
            k=COMPONENTS.index(component)
            for row,value in zip(result,channel['values']):
                if len(value)<=k:raise ValueError('Missing channel component')
                row[axis]+=coefficient*value[k]
    if any(not math.isfinite(x) for row in result for x in row):
        raise ValueError('Nonfinite evaluated coordinate')
    return [tuple(row) for row in result]
