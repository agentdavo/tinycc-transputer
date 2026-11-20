#!/usr/bin/env python3
"""Generate raw-opcode FP probes with integer-only, bit-exact test oracles."""
import argparse, json, struct
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cpu',choices=['t805','t9000'],required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--softfloat-vectors',type=Path);p.add_argument('--softfloat-only',action='store_true');p.add_argument('--tvx-traps',action='store_true');p.add_argument('--tvx-soft',action='store_true');p.add_argument('--tvx-protected',action='store_true');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
if (a.tvx_traps or a.tvx_soft or a.tvx_protected) and a.cpu!='t9000': p.error('TVX trap probes require --cpu t9000')

def ins(op,n):
    n &= 0xffffffff
    if n & 0x80000000:
        prefix=ins(0x60,((~n)&0xffffffff)>>4)
        # negative operand encoded via complemented high nibbles
        return prefix+[op|(n&15)]
    digits=[]
    while n>15: digits.append(n&15);n>>=4
    return [0x20|n]+[0x20|v for v in reversed(digits[1:])]+[op|digits[0]] if digits else [op|n]
def raw(bs):return '.byte '+','.join(hex(x) for x in bs)
def op(n):return raw(ins(0xf0,n))
def ldc(n):return raw(ins(0x40,n))
def ptr(w):return 'ldl 1\n'+(f'ldnlp {w}\n' if w else '')
def load(w,k):return ptr(w)+op(0x8e if k=='sn' else 0x8a)+'\n'
def store(w,k):return ptr(w)+op(0x88 if k=='sn' else 0x84)+'\n'
def integer(w):return ptr(0)+f'stnl {w}\n'
def entry(n):return ldc(n)+'\n'+op(0xab)+'\n'
mode={0x22:0xd0,4:0xd4,5:0xd5,6:0xd6,7:0xd7,8:0xd8,9:0xd9,10:0xda,11:0xdb,14:0xde,15:0xdf,17:0xd1,18:0xd2,0x23:0xcb}
def special(n):return op(mode[n])+'\n' if a.cpu=='t9000' else entry(n)
def bits(x,k):return list(struct.unpack('<I' if k=='sn' else '<II',struct.pack('<f' if k=='sn' else '<d',x)))
cases=[]
def add(name,code,initial,expected,ops):
    i=len(cases);v=[0]*24
    for w,val in initial.items():
        if isinstance(val,list):v[w:w+len(val)]=val
        else:v[w]=val&0xffffffff
    cases.append(dict(name=name,code=code,input=v,expected=expected,ops=ops,id=i))
for k in ['sn','db']:
    for label,x in [('one',1.),('negative',-2.5),('negzero',-0.),('tiny',2.**(-149 if k=='sn' else -1074)),('infinity',float('inf'))]:
        b=bits(x,k);add('load-store-'+k+'-'+label,load(0,k)+store(4,k),{0:b},{4+j:v for j,v in enumerate(b)},['fpldnl'+k,'fpstnl'+k])
    for name,n,x,y,z in [('fpadd',0x87,1.5,2.25,3.75),('fpsub',0x89,7.,2.,5.),('fpmul',0x8b,-3.,2.,-6.),('fpdiv',0x8c,7.,2.,3.5)]:
        b=bits(z,k);add(name+'-'+k,load(0,k)+load(2,k)+op(n)+'\n'+store(4,k),{0:bits(x,k),2:bits(y,k)},{4+j:v for j,v in enumerate(b)},[name])
    for name,n,val in [('fpgt',0x94,1),('fpeq',0x95,0),('fpordered',0x92,1)]+([('fpge',0x97,1),('fplg',0x9b,1)] if a.cpu=='t9000' else []):
        add(name+'-'+k,load(0,k)+load(2,k)+op(n)+'\n'+integer(4),{0:bits(3.,k),2:bits(2.,k)},{4:val},[name])
    qnan=[0x7fc00000] if k=='sn' else [0,0x7ff80000]
    for name,n,vec,want in [('fpnan',0x91,qnan,1),('fpnan',0x91,bits(1.,k),0),('fpnotfinite',0x93,bits(float('inf'),k),1),('fpnotfinite',0x93,bits(1.,k),0)]:
        add(name+'-'+k+'-'+str(want),load(0,k)+op(n)+'\n'+integer(4),{0:vec},{4:want},[name])
    for name,n in [('fpordered',0x92),('fpeq',0x95),('fpgt',0x94)]:
        add(name+'-'+k+'-nan',load(0,k)+load(2,k)+(op(0x92)+'\n' if a.cpu=='t805' and name=='fpgt' else '')+op(n)+'\n'+(op(0x46)+'\n' if a.cpu=='t805' and name=='fpgt' else '')+integer(4),{0:qnan,2:bits(1.,k)},{4:0},[name])
    for name,n,off in [('indexed-load',0x86 if k=='sn' else 0x82,1 if k=='sn' else 2)]:
        b=bits(6.25,k);add(name+'-'+k,ldc(1)+'\n'+ptr(0)+op(n)+'\n'+store(4,k),{off:b},{4+j:v for j,v in enumerate(b)},['fpldnl'+k+'i'])
    for name,n in [('add',0xaa if k=='sn' else 0xa6),('mul',0xac if k=='sn' else 0xa8)]:
        b=bits(5. if name=='add' else 6.,k);add('fpldnl'+name+k,load(0,k)+ptr(2)+op(n)+'\n'+store(4,k),{0:bits(2.,k),2:bits(3.,k)},{4+j:v for j,v in enumerate(b)},['fpldnl'+name+k])
    add('fpdup-fprev-'+k,load(0,k)+op(0xa3)+'\n'+load(2,k)+op(0xa4)+'\n'+store(4,k)+store(6,k)+store(8,k),{0:bits(2.,k),2:bits(3.,k)},{**{4+j:v for j,v in enumerate(bits(2.,k))},**{6+j:v for j,v in enumerate(bits(3.,k))},**{8+j:v for j,v in enumerate(bits(2.,k))}},['fpdup','fprev'])
    add('fpldzero'+k,op(0x9f if k=='sn' else 0xa0)+'\n'+store(4,k),{},{4+j:0 for j in range(1 if k=='sn' else 2)},['fpldzero'+k])
    for name,n,x,z in [('abs',11,-3.,3.),('mulby2',18,1.5,3.),('divby2',17,3.,1.5),('expinc32',10,1.,2.**32),('expdec32',9,2.**32,1.)]:
        b=bits(z,k);add(('fp' if a.cpu=='t9000' else 'fpu')+name+'-'+k,load(0,k)+special(n)+store(4,k),{0:bits(x,k)},{4+j:v for j,v in enumerate(b)},[('fp' if a.cpu=='t9000' else 'fpu')+name])
    for name,n,x,z in [('rn',0x22,2.5,2.),('rp',4,2.5,3.),('rm',5,-2.5,-3.),('rz',6,-2.5,-2.)]:
        b=bits(z,k);add('round-'+name+'-'+k,load(0,k)+special(n)+op(0xa1)+'\n'+store(4,k),{0:bits(x,k)},{4+j:v for j,v in enumerate(b)},[('fp' if a.cpu=='t9000' else 'fpu')+name,'fpint'])
    sq=op(0xd3)+'\n' if a.cpu=='t9000' else entry(1)+entry(2)*(2 if k=='sn' else 5)+entry(3)
    add('sqrt-sequence-'+k,load(0,k)+sq+store(4,k),{0:bits(9.,k)},{4+j:v for j,v in enumerate(bits(3.,k))},['fpsqrt'] if a.cpu=='t9000' else ['fpusqrtfirst','fpusqrtstep','fpusqrtlast'])
    add('fprtoi32-fpstnli32-'+k,load(0,k)+op(0x9d)+'\n'+ptr(4)+op(0x9e)+'\n',{0:bits(-7.75,k)},{4:0xfffffff8},['fprtoi32','fpstnli32'])
    for size,sel in [(32,14),(64,15)]:
        for label,x,ok in [('valid',100.,1),('overflow',2.**size,0)]:
            add('check-i'+str(size)+'-'+k+'-'+label,load(0,k)+special(sel)+op(0x9c)+'\n'+integer(4),{0:bits(x,k)},{4:ok},[('fp' if a.cpu=='t9000' else 'fpu')+'chki'+str(size)])
    if a.cpu=='t9000': rem=op(0xcf)+'\n';ops=['fprem']
    else:
        i=len(cases); rem=op(0x8f)+f'\neqc 0\ncj remdone{i}\nremstep{i}:\n'+op(0x90)+f'\ncj remstep{i}\nremdone{i}:\n';ops=['fpremfirst','fpremstep (conditional)']
    add('remainder-'+k,load(0,k)+load(2,k)+rem+store(4,k),{0:bits(7.5,k),2:bits(2.,k)},{4+j:v for j,v in enumerate(bits(-.5,k))},ops)
    add('remainder-large-'+k,load(0,k)+load(2,k)+rem.replace('remstep'+str(len(cases)-1),'remstep'+str(len(cases))).replace('remdone'+str(len(cases)-1),'remdone'+str(len(cases)))+store(4,k),{0:bits(2.**100,k),2:bits(3.,k)},{4+j:v for j,v in enumerate(bits(1.,k))},ops)
    # State-format-independent diagnostic: save and reload one stack register.
    add('fpsttest-fpldtest-'+k,load(0,k)+ptr(12)+op(0x80)+'\n'+ptr(12)+op(0x85)+'\n'+store(4,k),{0:bits(1.5,k)},{4+j:v for j,v in enumerate(bits(1.5,k))},['fpsttest','fpldtest'])
for name,n,k,x in [('fpi32tor32',0x96,'sn',-12345),('fpi32tor64',0x98,'db',-2147483648),('fpb32tor64',0x9a,'db',0xffffffff)]:
    add(name,ptr(0)+op(n)+'\n'+store(4,k),{0:x},{4+j:v for j,v in enumerate(bits(float(x),k))},[name])
for src,dst,sel in [('sn','db',7),('db','sn',8)]:
    add('convert-'+src+'-'+dst,load(0,src)+special(sel)+store(4,dst),{0:bits(1.5,src)},{4+j:v for j,v in enumerate(bits(1.5,dst))},[('fp' if a.cpu=='t9000' else 'fpu')+'r'+('32tor64' if src=='sn' else '64tor32')])
if a.cpu=='t805':
    add('fpuclrerr',special(0x23)+entry(0x9c)+op(0x9c)+'\n'+integer(4),{},{4:1},['fpuclrerr'])
    add('fpunoround-exact',load(0,'db')+entry(13)+store(4,'sn'),{0:bits(1.,'db')},{4:0x3f800000},['fpunoround'])
add('set-test-clear-error',special(0x23)+op(0x9c)+'\n'+integer(4)+op(0x9c)+'\n'+integer(5),{},{4:0,5:1},[('fpseterr' if a.cpu=='t9000' else 'fpuseterr'),'fptesterr'])
add('fpchkerr-transfer',special(0x23)+op(0x83)+'\n'+op(0x29)+'\n'+integer(4),{},{4:0},['fpchkerr'])
if a.cpu=='t9000':
    add('fpclrerr',special(0x23)+op(0xdc)+'\n'+op(0x9c)+'\n'+integer(4),{},{4:1},['fpclrerr'])
    add('fpadddbsn',load(0,'db')+load(2,'db')+op(0xdd)+'\n'+store(4,'sn'),{0:bits(1.5,'db'),2:bits(2.25,'db')},{4:0x40700000},['fpadddbsn'])
    add('fprange',load(0,'sn')+load(2,'sn')+op(0x8d)+'\n'+store(4,'sn')+store(5,'sn'),{0:bits(7.5,'sn'),2:bits(2.,'sn')},{4:0xbf000000,5:0x40800000},['fprange'])
    add('fpstall-fpldall-mixed',load(0,'sn')+load(2,'db')+ptr(12)+op(-1)+'\n'+op(0x9f)+'\n'+ptr(12)+op(-2)+'\n'+store(4,'db')+store(6,'sn'),{0:bits(1.5,'sn'),2:bits(2.5,'db')},{4:0,5:0x40040000,6:0x3fc00000},['fpstall','fpldall'])
for k in ['sn','db']:
    add('fpeq-equal-'+k,load(0,k)+load(2,k)+op(0x95)+'\n'+integer(4),{0:bits(1.,k),2:bits(1.,k)},{4:1},['fpeq'])
    if a.cpu=='t9000':
        qnan=[0x7fc00000] if k=='sn' else [0,0x7ff80000]
        for name,n in [('fpge',0x97),('fplg',0x9b),('fpeq',0x95)]:
            add(name+'-nan-self-'+k,load(0,k)+load(2,k)+op(n)+'\n'+integer(4),{0:qnan,2:qnan},{4:0},[name])
    tiny=2.**(-149 if k=='sn' else -1074)
    for label,x,y,z in [('negative-zero',-0.,-0.,-0.),('subnormal',tiny,2*tiny,3*tiny),('cancellation',1.,-1.,0.)]:
        add('fpadd-edge-'+k+'-'+label,load(0,k)+load(2,k)+op(0x87)+'\n'+store(4,k),{0:bits(x,k),2:bits(y,k)},{4+j:v for j,v in enumerate(bits(z,k))},['fpadd'])
    add('fpdiv-by-zero-'+k,load(0,k)+load(2,k)+op(0x8c)+'\n'+store(4,k)+op(0x9c)+'\n'+integer(6),{0:bits(1.,k),2:bits(0.,k)},{**{4+j:v for j,v in enumerate(bits(float('inf'),k))},6:0},['fpdiv','fptesterr'])
    add('fpdiv-invalid-'+k,load(0,k)+load(2,k)+op(0x8c)+'\n'+op(0x91)+'\n'+integer(4)+op(0x9c)+'\n'+integer(5),{0:bits(0.,k),2:bits(0.,k)},{4:1,5:0},['fpdiv','fpnan','fptesterr'])
if a.cpu=='t9000':
    add('fpstall-round-mode',load(0,'db')+special(4)+ptr(12)+op(-1)+'\n'+op(0xa0)+'\n'+ptr(12)+op(-2)+'\n'+op(0xa1)+'\n'+store(4,'db'),{0:bits(2.5,'db')},{4:0,5:0x40080000},['fpstall','fpldall','fprp','fpint'])
    add('fpldall-keeps-cleared-error',special(0x23)+ptr(12)+op(-1)+'\n'+op(0xdc)+'\n'+ptr(12)+op(-2)+'\n'+op(0x9c)+'\n'+integer(4),{},{4:1},['fpstall','fpldall','fpseterr','fpclrerr','fptesterr'])
# Raw CPU-specific comparisons: preserve T800 bit ordering, reject every
# NaN on T9000, including either sign, signaling payloads and either operand.
for k in ['sn','db']:
    for label,high in [('qpos',0x7fc00001),('qneg',0xffc00001),('spos',0x7f800001),('sneg',0xff800001)]:
        nan=[high]
        if k=='db': nan=[1, (0xfff00000 if high & 0x80000000 else 0x7ff00000) | (0x80000 if label[0]=='q' else 0)]
        for which in ['left','right','self']:
            x,y=(nan,nan) if which=='self' else ((nan,bits(1.,k)) if which=='left' else (bits(1.,k),nan))
            want=0 if a.cpu=='t9000' else (1 if which=='self' else int((label.endswith('pos')) == (which=='left')))
            n=0x95 if which=='self' else 0x94
            add('raw-nan-'+k+'-'+label+'-'+which,load(0,k)+load(2,k)+op(n)+'\n'+integer(4),{0:x,2:y},{4:want},['fpeq' if which=='self' else 'fpgt'])
# Exact powers of two and explicit output words keep the boundary oracle
# independent of host arithmetic rounding and of the target compiler's FPU.
for k,frac,bias in [('sn',23,127),('db',52,1023)]:
    width=32 if k=='sn' else 64
    signbit=1<<(width-1)
    one=bias<<frac
    maxfinite=(((1<<(width-frac-1))-2)<<frac)|((1<<frac)-1)
    infinity=maxfinite+1
    words=lambda v:[v&0xffffffff] if k=='sn' else [v&0xffffffff,v>>32]
    for roundmode,sel in [('rn',0x22),('rp',4),('rm',5),('rz',6)]:
        for negative in [False,True]:
            sign=signbit if negative else 0
            away=(roundmode=='rm' if negative else roundmode=='rp')
            for boundary,offset in [('below',-1),('tie',0),('above',1)]:
                # Adjacent representable inputs around half an ULP at 1.0.
                half=(bias-frac-1)<<frac
                up=away or (roundmode=='rn' and offset>0)
                want=words(sign|one|int(up))
                code=load(0,k)+load(2,k)+special(sel)+op(0x87)+'\n'+store(4,k)
                code+=op(0x9c)+'\n'+integer(6)+op(0x9c)+'\n'+integer(7)
                add('boundary-add-'+k+'-'+roundmode+'-'+('neg-' if negative else 'pos-')+boundary,code,{0:words(sign|one),2:words(sign|(half+offset))},{**{4+j:v for j,v in enumerate(want)},6:1,7:1},['fpadd','fptesterr'])
            for label,x,y,opcode,result,error in [
                ('underflow-half',sign|1, bits(2.,k),0x8c,sign|int(away),1),
                ('overflow',sign|maxfinite,bits(2.,k),0x8b,sign|(infinity if roundmode=='rn' or away else maxfinite),0)]:
                code=load(0,k)+load(2,k)+special(sel)+op(opcode)+'\n'+store(4,k)
                code+=op(0x9c)+'\n'+integer(6)+op(0x9c)+'\n'+integer(7)
                add(label+'-'+k+'-'+roundmode+'-'+('neg' if negative else 'pos'),code,{0:words(x),2:y},{**{4+j:v for j,v in enumerate(words(result))},6:error,7:1},['fpdiv' if opcode==0x8c else 'fpmul','fptesterr'])
    # Exact normal-to-subnormal transition must not set aggregate FP_Error.
    code=load(0,k)+load(2,k)+op(0x8c)+'\n'+store(4,k)+op(0x9c)+'\n'+integer(6)
    add('underflow-exact-'+k,code,{0:words(1<<frac),2:bits(2.,k)},{**{4+j:v for j,v in enumerate(words(1<<(frac-1)))},6:1},['fpdiv','fptesterr'])
# Rounding selection applies to one FP instruction, including loads/stores.
for k in ['sn','db']:
    for name,sel,x,first,last in [('rp',4,2.5,3.,2.),('rm',5,-2.5,-3.,-2.),('rz',6,3.5,3.,4.)]:
        code=load(0,k)+op(0xa3)+'\n'+special(sel)+op(0xa1)+'\n'+store(4,k)+op(0xa1)+'\n'+store(6,k)
        add('round-reset-'+k+'-'+name,code,{0:bits(x,k)},{**{4+j:v for j,v in enumerate(bits(first,k))},**{6+j:v for j,v in enumerate(bits(last,k))}},['fpint','fpdup'])
for k in ['sn','db']:
    add('round-load-consumes-'+k,special(4)+load(0,k)+op(0xa1)+'\n'+store(4,k),{0:bits(2.5,k)},{4+j:v for j,v in enumerate(bits(2.,k))},['fpint','fpldnl'+k])
if a.cpu=='t9000':
    code=load(0,'db')+op(0xa3)+'\n'+special(4)+ptr(12)+op(-1)+'\n'+ptr(12)+op(-2)+'\n'+op(0xa1)+'\n'+store(4,'db')+op(0xa1)+'\n'+store(6,'db')
    add('round-restored-once',code,{0:bits(2.5,'db')},{4:0,5:0x40080000,6:0,7:0x40000000},['fpstall','fpldall','fpint'])
if a.cpu=='t9000':
    # Architectural StatusReg positions, not the former T4 two-bit encoding.
    add('ieee-stflags-mask',ldc(0xffffffff)+'\n'+op(0xb7)+'\n'+op(0xb6)+'\n'+integer(4)+ldc(0)+'\n'+op(0xb7)+'\n'+op(0xb6)+'\n'+integer(5),{},{4:0xbffcc,5:0},['stflags','ldflags'])
    for k in ['sn','db']:
        maxbits=[0x7f7fffff] if k=='sn' else [0xffffffff,0x7fefffff]
        tiny=[1] if k=='sn' else [1,0]
        minnorm=[0x800000] if k=='sn' else [0,0x100000]
        vectors=[('invalid',0x8c,bits(0.,k),bits(0.,k),0x104,8),
                 ('divide-zero',0x8c,bits(1.,k),bits(0.,k),0x404,9),
                 ('overflow',0x8b,maxbits,bits(2.,k),0x11004,10),
                 ('underflow',0x8c,tiny,bits(2.,k),0x14000,11),
                 ('inexact',0x8c,bits(1.,k),bits(3.,k),0x10000,12),
                 ('tiny-exact',0x8c,minnorm,bits(2.,k),0,11),
                 ('compound-overflow',0xaa if k=='sn' else 0xa6,maxbits,maxbits,0x11004,10)]
        for label,n,x,y,flags,etype in vectors:
            operands=load(0,k)+(ptr(2) if label=='compound-overflow' else load(2,k))
            code=operands+op(n)+'\n'+op(0xb6)+'\n'+integer(4)+op(0x9c)+'\n'+integer(5)+op(0xb6)+'\n'+integer(6)
            add('ieee-flags-'+k+'-'+label,code,{0:x,2:y},{4:flags,5:int(not(flags&4)),6:flags&~4},['ldflags','fptesterr'])
            enable=1<<({8:9,9:11,10:13,11:15,12:17}[etype])
            variants=[('specific',enable,etype)]
            if flags&4: variants.append(('aggregate',enable|8,7))
            if label in ['overflow','underflow']: variants.append(('priority',enable|(1<<17),etype))
            if label=='overflow':variants.append(('inexact-only',1<<17,12))
            for suffix,control,reason in variants:
                idx=len(cases)
                # THDS is io[8..19]; handler address io[21] is filled by C.
                setup=op(0xc2)+'\n'+integer(20)+ptr(21)+'ldnl 0\n'+integer(9)+ptr(8)+op(-7)+'\n'
                code=setup+ldc(control)+'\n'+op(0xb7)+'\n'+operands+op(n)+'\n'
                code+=ldc(1)+'\n'+integer(7)+ldc(0)+'\n'+op(0xb7)+'\n'+ptr(20)+'ldnl 0\n'+op(-7)+'\n'
                expect={**{j:v for j,v in enumerate(y)},**{2+j:v for j,v in enumerate(x)},4:reason,5:0,6:control,7:1,21:2}
                add('ieee-trap-'+k+'-'+label+'-'+suffix,code,{0:x,2:y,10:0x80000000,11:0x80000000},expect,['stflags','ldflags','selth','tret'])
                # Wptr points at io[8]. Save trap reason/type and pre-trap controls.
                cases[-1]['handler']='stl -3\nstl -4\nldl 8\nldl 4\n'+op(0x04)+'\nstl 13\nldl 0\n'+ldc(0xbffcc)+'\n'+op(0x46)+'\nstl -2\nldlp -8\n'+op(0x88 if k=='sn' else 0x84)+'\nldlp -6\n'+op(0x88 if k=='sn' else 0x84)+'\n'+ldc(0)+'\nstl 0\n'+ldc(0)+'\n'+op(-5)+'\n'
if a.cpu=='t9000':
    for k in ['sn','db']:
        qnan=[0x7fc00001] if k=='sn' else [1,0x7ff80000]
        snan=[0x7f800001] if k=='sn' else [1,0x7ff00000]
        for label,nan,want in [('quiet',qnan,4),('signaling',snan,0x104)]:
            add('ieee-nan-load-'+k+'-'+label,load(0,k)+op(0xb6)+'\n'+integer(4),{0:nan},{4:0},['ldflags','fpldnl'+k])
            add('ieee-nan-add-'+k+'-'+label,load(0,k)+load(2,k)+op(0x87)+'\n'+op(0xb6)+'\n'+integer(4),{0:nan,2:bits(1.,k)},{4:want},['fpadd','ldflags'])
    for label,n,x,y,control,etype in [('invalid',0x8c,0.,0.,1<<9,8),('underflow-exact',0x8c,2.**-1022,2.,1<<15,11)]:
        # Identity RODS supplied by C in io[20]; private P workspace at 0x8003c000.
        code=ptr(21)+'ldnl 0\n'+integer(13)+ptr(0)+ldc(0x8003c004)+'\n'+'stnl 0\n'
        code+=ptr(20)+'ldnl 0\n'+ptr(8)+op(-6)+'\n'
        code+=integer(5)+integer(4)+ptr(8)+'ldnl 0\n'+ldc(0xbffcc)+'\n'+op(0x46)+'\n'+integer(6)
        code+=store(0,'db')+store(2,'db')+ldc(1)+'\n'+integer(7)
        add('ieee-p-trap-'+label,code,{0:bits(x,'db'),2:bits(y,'db'),8:control,12:0x8003c000},{**{j:v for j,v in enumerate(bits(y,'db'))},**{2+j:v for j,v in enumerate(bits(x,'db'))},4:etype,5:0,6:control,7:1},['goprot','fpdiv'])
        cases[-1]['handler']=load(0,'db')+load(2,'db')+op(n)+'\n'+op(-8)+'\n'
if a.cpu=='t9000':
    for k in ['sn','db']:
        for label,nan in [('quiet',[0x7fc00001] if k=='sn' else [1,0x7ff80000]),('signaling',[0x7f800001] if k=='sn' else [1,0x7ff00000])]:
            for name,n in [('fpgt',0x94),('fpge',0x97),('fplg',0x9b),('fpeq',0x95),('fpordered',0x92)]:
                flags=0x104 if label=='signaling' or n in [0x94,0x97,0x9b] else 4
                add('ieee-compare-'+k+'-'+label+'-'+name,load(0,k)+load(2,k)+op(n)+'\n'+integer(4)+op(0xb6)+'\n'+integer(5),{0:nan,2:bits(1.,k)},{4:0,5:flags},[name,'ldflags'])
        for width,sel in [(32,14),(64,15)]:
            add('ieee-range-'+k+'-'+str(width),load(0,k)+special(sel)+op(0xb6)+'\n'+integer(4),{0:bits(2.**width,k)},{4:0x104},['fpchki'+str(width),'ldflags'])
if a.cpu=='t9000':
    handler=next(c['handler'] for c in cases if c['name']=='ieee-trap-db-invalid-specific')
    for label,control,etype,value,prior in [('round',1<<17,12,2147483647.5,0),('range',1<<9,8,2147483648.,1<<16),('aggregate',8,7,2147483648.,1<<16)]:
        setup=op(0xc2)+'\n'+integer(20)+ptr(21)+'ldnl 0\n'+integer(9)+ptr(8)+op(-7)+'\n'
        code=setup+ldc(control)+'\n'+op(0xb7)+'\n'+load(2,'db')+load(0,'db')+op(0x9d)+'\n'
        code+=ldc(1)+'\n'+integer(7)+ldc(0)+'\n'+op(0xb7)+'\n'+ptr(20)+'ldnl 0\n'+op(-7)+'\n'
        add('ieee-compound-rtoi-'+label,code,{0:bits(2147483647.5,'db'),2:bits(1.,'db'),10:0x80000000,11:0x80000000},{**{j:v for j,v in enumerate(bits(value,'db'))},2:0,3:0x3ff00000,4:etype,5:0,6:control|prior,7:1,21:2},['fprtoi32','tret'])
        cases[-1]['handler']=handler
if a.cpu=='t9000':
    handler=next(c['handler'] for c in cases if c['name']=='ieee-trap-db-invalid-specific')
    maxdb=struct.unpack('<d',struct.pack('<Q',0x7fefffffffffffff))[0]
    for label,x,y,control,etype,fa,fb in [('add-overflow',maxdb,maxdb,1<<13,10,maxdb,maxdb),('convert-overflow',2.**127,2.**127,1<<13,10,2.**128,0.),('add-inexact',1.,2.**-53,1<<17,12,2.**-53,1.),('convert-inexact',1.,2.**-24,1<<17,12,1.+2.**-24,0.)]:
        setup=op(0xc2)+'\n'+integer(20)+ptr(21)+'ldnl 0\n'+integer(9)+ptr(8)+op(-7)+'\n'
        code=setup+ldc(control)+'\n'+op(0xb7)+'\n'+load(0,'db')+load(2,'db')+op(0xdd)+'\n'
        code+=ldc(1)+'\n'+integer(7)+ldc(0)+'\n'+op(0xb7)+'\n'+ptr(20)+'ldnl 0\n'+op(-7)+'\n'
        add('ieee-compound-adddbsn-'+label,code,{0:bits(x,'db'),2:bits(y,'db'),10:0x80000000,11:0x80000000},{**{j:v for j,v in enumerate(bits(fa,'db'))},**{2+j:v for j,v in enumerate(bits(fb,'db'))},4:etype,5:0,6:control,7:1,21:2},['fpadddbsn','tret'])
        cases[-1]['handler']=handler
if a.cpu=='t9000':
    # Independent architectural images, including every valid type combination.
    for mode_name,sel,round_bits in [('rz',6,0),('rn',0x22,1),('rp',4,2),('rm',5,3)]:
        for types in range(8):
            kinds=['db' if types&(1<<i) else 'sn' for i in range(3)]
            status=round_bits | sum((int(k=='db') << (2+2*i)) for i,k in enumerate(kinds))
            initial={0:bits(1.5,kinds[0]),2:bits(2.5,kinds[1]),4:bits(3.5,kinds[2])}
            expected={12:status}
            for i,k in enumerate(kinds):
                data=bits(1.5+i,k)
                expected[13+2*i]=data[0]
                expected[14+2*i]=data[1] if k=='db' else 0
            code=load(4,kinds[2])+load(2,kinds[1])+load(0,kinds[0])+special(sel)+ptr(12)+op(-1)+'\n'
            add('fp-image-'+mode_name+'-'+str(types),code,initial,expected,['fpstall'])
        # Load an externally constructed image; its rounding must affect fpint.
        initial={12:round_bits|0x44,13:bits(2.5,'db'),15:bits(1.5,'sn'),17:bits(-3.5,'db')}
        code=ptr(12)+op(-2)+'\n'+op(0xa1)+'\n'+store(4,'db')+store(6,'sn')+store(8,'db')
        add('fp-image-load-'+mode_name,code,initial,{4:0,5:0x40080000 if mode_name=='rp' else 0x40000000,6:0x3fc00000,8:0,9:0xc00c0000},['fpldall','fpint'])
    code=ptr(12)+op(-1)+'\n'+special(0x23)+ptr(12)+op(-2)+'\n'+op(0xb6)+'\n'+integer(4)
    add('fpldall-keeps-set-error',code,{},{4:4},['fpstall','fpldall','ldflags'])
    # Non-FP traps must also save StatusReg, not overwrite it with a cause code.
    code=ptr(21)+'ldnl 0\n'+integer(13)+ptr(0)+ldc(0x8003c004)+'\nstnl 0\n'
    code+=ptr(20)+'ldnl 0\n'+ptr(8)+op(-6)+'\n'+integer(5)+integer(4)
    code+=ptr(8)+'ldnl 0\n'+ldc(0xbffcc)+'\n'+op(0x46)+'\n'+integer(6)
    add('p-syscall-saves-status',code,{8:0x104,12:0x8003c000},{4:0,5:8,6:0x104},['goprot','syscall'])
    cases[-1]['handler']=op(-8)+'\n'
if a.cpu=='t9000':
    # ISM Table 11.31: distinguish FA from FB and signaling priority with
    # different signs/payloads. Loads are in reverse evaluation-stack order.
    for k in ['sn','db']:
        qa=[0xffc00123] if k=='sn' else [0x123,0xfff80000]
        qb=[0x7fc00456] if k=='sn' else [0x456,0x7ff80000]
        sa=[0xff800123] if k=='sn' else [0x123,0xfff00000]
        sb=[0x7f800456] if k=='sn' else [0x456,0x7ff00000]
        num=bits(1.,k)
        for label,fa,fb,result,flags in [
            ('ss',sa,sb,qa,0x104),('sq',sa,qb,qa,0x104),
            ('qs',qa,sb,qb,0x104),('qq',qa,qb,qa,4),
            ('sn',sa,num,qa,0x104),('qn',qa,num,qa,4),
            ('ns',num,sb,qb,0x104),('nq',num,qb,qb,4)]:
            code=load(2,k)+load(0,k)+op(0x87)+'\n'+store(4,k)+op(0xb6)+'\n'+integer(6)
            add('nan-propagation-'+k+'-'+label,code,{0:fa,2:fb},{**{4+j:v for j,v in enumerate(result)},6:flags},['fpadd','ldflags'])
        for name,n in [('fpsub',0x89),('fpmul',0x8b),('fpdiv',0x8c)]:
            code=load(2,k)+load(0,k)+op(n)+'\n'+store(4,k)+op(0xb6)+'\n'+integer(6)
            add('nan-propagation-'+k+'-'+name,code,{0:qa,2:sb},{**{4+j:v for j,v in enumerate(qb)},6:0x104},[name,'ldflags'])
    # Trap before conversion commits: preserve source precision and payload,
    # check saved controls, and prove tret resumes after the faulting opcode.
    for k,selector,label,value,enable,reason in [
        ('sn',7,'widen-snan',[0xff800123],1<<9,8),
        ('sn',7,'widen-qnan',[0xffc00123],8,7),
        ('db',8,'narrow-snan',[0x123,0xfff00000],1<<9,8),
        ('db',8,'narrow-qnan',[0x123,0xfff80000],8,7),
        ('db',8,'narrow-overflow',bits(2.**128,'db'),1<<13,10),
        ('db',8,'narrow-inexact',bits(1.+2.**-24,'db'),1<<17,12),
        ('db',8,'narrow-underflow',bits(2.**-150,'db'),1<<15,11),
        ('db',8,'narrow-tiny-exact',bits(2.**-149,'db'),1<<15,11)]:
        handler=next(c['handler'] for c in cases if c['name']=='ieee-trap-'+k+'-invalid-specific')
        setup=op(0xc2)+'\n'+integer(20)+ptr(21)+'ldnl 0\n'+integer(9)+ptr(8)+op(-7)+'\n'
        code=setup+ldc(enable)+'\n'+op(0xb7)+'\n'+load(2,k)+load(0,k)+special(selector)
        code+=ldc(1)+'\n'+integer(7)+ldc(0)+'\n'+op(0xb7)+'\n'+ptr(20)+'ldnl 0\n'+op(-7)+'\n'
        sentinel=bits(1.,k)
        add('conversion-trap-'+label,code,{0:value,2:sentinel,10:0x80000000,11:0x80000000},{**{j:v for j,v in enumerate(value)},**{2+j:v for j,v in enumerate(sentinel)},4:reason,5:0,6:enable,7:1,21:2},['fpr32tor64' if selector==7 else 'fpr64tor32','selth','tret'])
        cases[-1]['handler']=handler
if a.cpu=='t9000':
    # ISM 11.29 and opcode definitions: generated NaNs are distinct from
    # propagated payloads; the quotient on exceptional fprange is undefined.
    for k in ['sn','db']:
        qa=[0xffc00123] if k=='sn' else [0x123,0xfff80000]
        sb=[0x7f800456] if k=='sn' else [0x456,0x7ff00000]
        qb=[0x7fc00456] if k=='sn' else [0x456,0x7ff80000]
        for name,n in [('fprem',0xcf),('fprange',0x8d)]:
            vectors=[('nan-priority',qa,sb,qb,0x104),
                     ('quiet',qa,bits(1.,k),qa,4),
                     ('infinite-dividend',bits(2.,k),bits(float('inf'),k),[0x7fc04000] if k=='sn' else [0,0x7ff80800],0x104),
                     ('zero-divisor',bits(0.,k),bits(1.,k),[0x7fc02000] if k=='sn' else [0,0x7ff80400],0x104),
                     ('infinite-divisor',bits(float('inf'),k),bits(-3.,k),bits(-3.,k),4)]
            if name=='fprange':
                vectors.append(('quotient-limit',bits(1.,k),bits(2.**(24 if k=='sn' else 53),k),[0x7fc01000] if k=='sn' else [0,0x7ff80200],0x104))
                vectors.append(('negative-quotient-limit',bits(1.,k),bits(-2.**(24 if k=='sn' else 53),k),[0x7fc01000] if k=='sn' else [0,0x7ff80200],0x104))
            for label,fa,fb,result,flags in vectors:
                code=load(2,k)+load(0,k)+op(n)+'\n'+store(4,k)+op(0xb6)+'\n'+integer(6)
                add(name+'-edge-'+k+'-'+label,code,{0:fa,2:fb},{**{4+j:v for j,v in enumerate(result)},6:flags},[name,'ldflags'])
        limit=bits(2.**(24 if k=='sn' else 53)-1.,k)
        code=load(2,k)+load(0,k)+op(0x8d)+'\n'+store(4,k)+store(6,k)+op(0xb6)+'\n'+integer(8)
        add('fprange-last-valid-'+k,code,{0:bits(1.,k),2:limit},{**{4+j:v for j,v in enumerate(bits(0.,k))},**{6+j:v for j,v in enumerate(limit)},8:0},['fprange','ldflags'])
    for label,fa,fb,flags in [('quiet-a',[0x123,0xfff80000],bits(1.,'db'),4),('signal-b',bits(1.,'db'),[0x456,0x7ff00000],0x104),('both',[0x123,0xfff80000],[0x456,0x7ff00000],0x104)]:
        code=load(2,'db')+load(0,'db')+op(0xdd)+'\n'+store(4,'sn')+op(0xb6)+'\n'+integer(6)
        add('fpadddbsn-nan-'+label,code,{0:fa,2:fb},{4:0x7fc20000,6:flags},['fpadddbsn','ldflags'])
    for name,n,fa,fb,enable,reason in [
        ('fpadddbsn-snan',0xdd,[0x123,0xfff00000],bits(1.,'db'),1<<9,8),
        ('fpadddbsn-qnan',0xdd,[0x123,0xfff80000],bits(1.,'db'),8,7),
        ('fprem-zero',0xcf,bits(0.,'db'),bits(1.,'db'),1<<9,8),
        ('fprem-tiny',0xcf,bits(2.**-1022,'db'),[1,0],1<<15,11),
        ('fprange-limit',0x8d,bits(1.,'db'),bits(2.**53,'db'),1<<9,8),
        ('fprange-snan',0x8d,bits(1.,'db'),[0x123,0xfff00000],1<<9,8)]:
        setup=op(0xc2)+'\n'+integer(20)+ptr(21)+'ldnl 0\n'+integer(9)+ptr(8)+op(-7)+'\n'
        code=setup+ldc(enable)+'\n'+op(0xb7)+'\n'+load(2,'db')+load(0,'db')+op(n)+'\n'
        code+=ldc(1)+'\n'+integer(7)+ldc(0)+'\n'+op(0xb7)+'\n'+ptr(20)+'ldnl 0\n'+op(-7)+'\n'
        add('edge-trap-'+name,code,{0:fa,2:fb,10:0x80000000,11:0x80000000},{**{j:v for j,v in enumerate(fa)},**{2+j:v for j,v in enumerate(fb)},4:reason,5:0,6:enable,7:1,21:2},[name.split('-')[0],'selth','tret'])
        cases[-1]['handler']=next(c['handler'] for c in cases if c['name']=='ieee-trap-db-invalid-specific')
if a.cpu=='t9000':
    for mode_name,sel in [('rn',0x22),('rp',4),('rm',5),('rz',6)]:
        for negative in [False,True]:
            sign=-1. if negative else 1.
            up=mode_name==('rm' if negative else 'rp')
            result=(0x80000000 if negative else 0)|0x3f800000|int(up)
            code=load(0,'db')+load(2,'db')+special(sel)+op(0xdd)+'\n'+store(4,'sn')+op(0xb6)+'\n'+integer(6)
            add('adddbsn-tie-'+mode_name+('-neg' if negative else '-pos'),code,{0:bits(sign,'db'),2:bits(sign*2.**-24,'db')},{4:result,6:1<<16},['fpadddbsn','ldflags'])
            tiny_result=(0x80000000 if negative else 0)|int(up)
            add('adddbsn-tiny-'+mode_name+('-neg' if negative else '-pos'),code,{0:bits(sign*2.**-150,'db'),2:bits(sign*0.,'db')},{4:tiny_result,6:0x14000},['fpadddbsn','ldflags'])
        code=load(0,'db')+load(2,'db')+special(sel)+op(0xdd)+'\n'+store(4,'sn')+op(0xb6)+'\n'+integer(6)
        add('adddbsn-cancel-'+mode_name,code,{0:bits(1.,'db'),2:bits(-1.,'db')},{4:0x80000000 if mode_name=='rm' else 0,6:0},['fpadddbsn','ldflags'])
    # Exact sums lie immediately either side of a single-precision midpoint;
    # rounding to double first would incorrectly lose the +/- 2^-54 term.
    for label,offset,result in [('below',-2.**-54,0x3f800000),('above',2.**-54,0x3f800001)]:
        code=load(0,'db')+load(2,'db')+op(0xdd)+'\n'+store(4,'sn')+op(0xb6)+'\n'+integer(6)
        add('adddbsn-double-round-'+label,code,{0:bits(1.+2.**-24,'db'),2:bits(offset,'db')},{4:result,6:1<<16},['fpadddbsn','ldflags'])
    for label,x,y,sel,control,reason,fa,fb,round_bits in [
        ('add-rp',1.,2.**-53,4,1<<17,12,2.**-53,1.,2),
        ('convert-rm',1.,2.**-24,5,1<<17,12,1.+2.**-24,0.,1),
        ('tiny-rz',2.**-150,0.,6,1<<15,11,2.**-150,0.,1),
        ('overflow-priority',2.**127,2.**127,4,(1<<13)|(1<<17)|8,7,2.**128,0.,1)]:
        setup=op(0xc2)+'\n'+integer(20)+ptr(21)+'ldnl 0\n'+integer(9)+ptr(8)+op(-7)+'\n'
        code=setup+ldc(control)+'\n'+op(0xb7)+'\n'+load(0,'db')+load(2,'db')+special(sel)+op(0xdd)+'\n'
        code+=ldc(1)+'\n'+integer(7)+ldc(0)+'\n'+op(0xb7)+'\n'+ptr(20)+'ldnl 0\n'+op(-7)+'\n'
        add('adddbsn-trap-state-'+label,code,{0:bits(x,'db'),2:bits(y,'db'),10:0x80000000,11:0x80000000},{**{j:v for j,v in enumerate(bits(fa,'db'))},**{2+j:v for j,v in enumerate(bits(fb,'db'))},4:reason,5:round_bits,6:control,7:1,21:2},['fpadddbsn','fpstall','tret'])
        handler=next(c['handler'] for c in cases if c['name']=='ieee-trap-db-invalid-specific')
        # Scratch is in the reserved page below P workspace. Snapshot before
        # the handler's FP stores consume the restored rounding selection.
        snapshot=ldc(0x8003b100)+'\n'+op(-1)+'\n'+ldc(0x8003b100)+'\nldnl 0\n'+ldc(3)+'\n'+op(0x46)+'\nstl -3\n'
        cases[-1]['handler']=handler.replace('stl -3\nstl -4\n','stl -3\nstl -4\n'+snapshot,1)
if a.cpu=='t9000':
    for k in ['sn','db']:
        snan=[0xff800123] if k=='sn' else [0x123,0xfff00000]
        quiet=[0xffc00123] if k=='sn' else [0x123,0xfff80000]
        for label,value in [('snan',snan),('negative-zero',bits(-0.,k))]:
            # Loads, stores and stack operations do not quiet NaNs or raise
            # IEEE/aggregate error. Preserve an unrelated sticky inexact bit.
            for indexed in [False,True]:
                loading=ldc(0)+'\n'+ptr(0)+op(0x86 if k=='sn' else 0x82)+'\n' if indexed else load(0,k)
                code=ldc(1<<16)+'\n'+op(0xb7)+'\n'+loading+op(0xa3)+'\n'+load(2,k)+op(0xa4)+'\n'+store(4,k)+store(6,k)+store(8,k)+op(0xb6)+'\n'+integer(10)
                expected={**{4+j:v for j,v in enumerate(value)},**{6+j:v for j,v in enumerate(bits(1.,k))},**{8+j:v for j,v in enumerate(value)},10:1<<16}
                add('passive-'+k+'-'+label+('-indexed' if indexed else ''),code,{0:value,2:bits(1.,k)},expected,['fpldnl'+k+('i' if indexed else ''),'fpstnl'+k,'fpdup','fprev','ldflags'])
        for name,n in [('fpdivby2',0xd1),('fpmulby2',0xd2),('fpexpdec32',0xd9),('fpexpinc32',0xda),('fpsqrt',0xd3),('fpint',0xa1)]:
            for label,value,flags in [('snan',snan,0x104),('qnan',quiet,4)]:
                code=load(0,k)+op(n)+'\n'+store(4,k)+op(0xb6)+'\n'+integer(6)
                add('unary-nan-'+k+'-'+name+'-'+label,code,{0:value},{**{4+j:v for j,v in enumerate(quiet)},6:flags},[name,'ldflags'])
        for name,n,want in [('fpnan',0x91,1),('fpnotfinite',0x93,1)]:
            code=load(0,k)+op(n)+'\n'+integer(4)+store(6,k)+op(0xb6)+'\n'+integer(8)
            add('classify-snan-'+k+'-'+name,code,{0:snan},{4:want,**{6+j:v for j,v in enumerate(snan)},8:0},[name,'ldflags'])
        for width,n in [(32,0xde),(64,0xdf)]:
            for label,value,flags in [('minimum',-2.**(width-1),0),('upper',2.**(width-1),0x104)]:
                code=load(0,k)+op(n)+'\n'+store(4,k)+op(0xb6)+'\n'+integer(6)
                add('range-bound-'+k+'-'+str(width)+'-'+label,code,{0:bits(value,k)},{**{4+j:v for j,v in enumerate(bits(value,k))},6:flags},['fpchki'+str(width),'ldflags'])
        maxvalue=[0x7f7fffff] if k=='sn' else [0xffffffff,0x7fefffff]
        for name,n,value,enable,reason in [('fpsqrt',0xd3,bits(-1.,k),1<<9,8),('fpdivby2',0xd1,[1] if k=='sn' else [1,0],1<<15,11),('fpmulby2',0xd2,maxvalue,1<<13,10),('fpexpdec32',0xd9,[1] if k=='sn' else [1,0],1<<15,11),('fpexpinc32',0xda,maxvalue,1<<13,10)]:
            setup=op(0xc2)+'\n'+integer(20)+ptr(21)+'ldnl 0\n'+integer(9)+ptr(8)+op(-7)+'\n'
            code=setup+ldc(enable)+'\n'+op(0xb7)+'\n'+load(2,k)+load(0,k)+op(n)+'\n'
            code+=ldc(1)+'\n'+integer(7)+ldc(0)+'\n'+op(0xb7)+'\n'+ptr(20)+'ldnl 0\n'+op(-7)+'\n'
            sentinel=bits(1.,k)
            add('unary-trap-'+k+'-'+name,code,{0:value,2:sentinel,10:0x80000000,11:0x80000000},{**{j:v for j,v in enumerate(value)},**{2+j:v for j,v in enumerate(sentinel)},4:reason,5:0,6:enable,7:1,21:2},[name,'selth','tret'])
            cases[-1]['handler']=next(c['handler'] for c in cases if c['name']=='ieee-trap-'+k+'-invalid-specific')
if a.cpu=='t9000':
    from audit_opcodes import ARCH
    # Reuse exact-value probes, inserting a non-default rounding mode just
    # before the target opcode and snapshotting it before another FP opcode.
    # Mode selectors/save-restore intentionally differ; rounding-dependent
    # fpint/fprtoi32 have dedicated boundary/reset probes elsewhere.
    excluded={'fprn','fprp','fprm','fprz','fpstall','fpldall','fpint','fprtoi32'}
    for name,n in ARCH.items():
        if name in excluded: continue
        source=next(c for c in cases if name in c['ops'] and 'handler' not in c)
        needle=op(n)+'\n'
        assert needle in source['code'], name
        snapshot=ldc(0x8003b100)+'\n'+op(-1)+'\n'+ldc(0x8003b100)+'\nldnl 0\n'+ldc(3)+'\n'+op(0x46)+'\n'+integer(19)
        code=source['code'].replace(needle,special(4)+needle+snapshot,1)
        add('round-consumed-'+name,code,{i:v for i,v in enumerate(source['input']) if v},{**source['expected'],19:1},[name,'fprp','fpstall'])
# ISM 11.7 fpabs explicitly excludes NaNs from sign clearing; 11.30
# specifies quieting and exception flags. These are not host fabs rules.
for k in ['sn','db']:
    for negative in [False,True]:
        for signaling in [False,True]:
            nan_bits=(0x7f800001 if signaling else 0x7fc00001) if k=='sn' else (0x7ff0000000000001 if signaling else 0x7ff8000000000001)
            if negative: nan_bits|=1<<(31 if k=='sn' else 63)
            quiet=nan_bits | (0x400000 if k=='sn' else 0x8000000000000)
            data=[nan_bits&0xffffffff] if k=='sn' else [nan_bits&0xffffffff,nan_bits>>32]
            expected={4:quiet&0xffffffff,6:0x104 if signaling else 4}
            if k=='db': expected[5]=quiet>>32
            if a.cpu=='t9000':
                code=load(0,k)+special(11)+op(0xb6)+'\n'+integer(6)+store(4,k)
            else:
                # T805 keeps its legacy sign-clearing behavior. Check
                # classification/sign/error without imposing T9000 quieting.
                code=load(0,k)+special(11)+op(0x91)+'\n'+integer(6)+op(0x9c)+'\n'+integer(7)+store(4,k)
                code+=ptr(4 if k=='sn' else 5)+'ldnl 0\n'+ldc(0x80000000)+'\n'+op(0x46)+'\n'+integer(8)
                expected={6:1,7:0,8:0}
            add('fpabs-nan-'+k+('-negative' if negative else '-positive')+('-signaling' if signaling else '-quiet'),code,{0:data},expected,['fpabs','ldflags'] if a.cpu=='t9000' else ['fpuabs','fpnan','fptesterr'])
if a.softfloat_only:
    if not a.softfloat_vectors: p.error('--softfloat-only requires --softfloat-vectors')
    cases.clear()
if a.softfloat_vectors:
    reference=json.loads(a.softfloat_vectors.read_text(encoding='utf-8'))
    for v in reference['vectors']:
        k='sn' if v['width']==32 else 'db'
        words=lambda value: [value&0xffffffff] if k=='sn' else [value&0xffffffff,value>>32]
        initial={0:words(int(v['a'],16)),2:words(int(v['b'],16))}
        outkind='sn' if v.get('result_width',v['width'])==32 else 'db'
        value=int(v['result'],16)
        expected={4:value&0xffffffff}
        if outkind=='db': expected[5]=value>>32
        if not v.get('check_result',True): expected={}
        elif v.get('nan_input'):
            if a.cpu=='t805': expected={4:1} # Classification; no legacy payload assumption.
            elif v['op']=='to_f32': expected={4:0x7fc20000} # ISM 11.29 R64ToR32NaN.
        rounding=special({'rn':0x22,'rp':4,'rm':5,'rz':6}[v['rounding']])
        code=load(0,k)
        if v['op'] in ['from_i32','from_u32']:
            code=rounding+ptr(0)+op(0x9a if v['op']=='from_u32' else 0x96 if k=='sn' else 0x98)+'\n'
        elif v['op'] in ['to_f32','to_f64']:
            code+=rounding+special(8 if v['op']=='to_f32' else 7)
        elif v['op']=='to_i32':
            code+=rounding+op(0x9d)+'\n'
        elif v['op']=='round_int':
            code+=rounding+op(0xa1)+'\n'
        elif v['op']=='sqrt':
            if a.cpu=='t805': code+=entry(1)+entry(2)*(2 if k=='sn' else 5)+rounding+entry(3)
            else: code+=rounding+op(0xd3)+'\n'
        else:
            code+=load(2,k)+rounding+op({'add':0x87,'sub':0x89,'mul':0x8b,'div':0x8c}[v['op']])+'\n'
        # Read flags before the store so the oracle covers the arithmetic itself.
        if a.cpu=='t9000':
            flags=v['flags']
            expected[6]=sum((1<<bit) for flag,bit in [(16,8),(8,10),(4,12),(2,14),(1,16)] if flags&flag)
            if flags&(16|8|4) or v.get('nan_input'): expected[6]|=4
            code+=op(0xb6)+'\n'+integer(6)
        if v.get('nan_input') or not v.get('check_result',True):
            code+=op(0x9c)+'\n'+integer(7)
            expected[7]=0
        if v.get('check_result',True):
            if v.get('nan_input') and a.cpu=='t805': code+=op(0x91)+'\n'+integer(4)
            else: code+=ptr(4)+op(0x9e)+'\n' if v['op']=='to_i32' else store(4,outkind)
        add(v['name'],code,initial,expected,['softfloat-reference',v['op']])
if a.tvx_traps:
    if a.tvx_soft: cases=[]
    vectors=[('snan',0x1ce,0x7f81,0x3f80,0x7fc1,0x104,1<<9,8),
             ('qnan',0x1ce,0xffc2,0x3f80,0xffc2,4,8,7),
             ('invalid',0x1cd,0x7f80,0,0x7fc0,0x104,1<<9,8),
             ('overflow',0x1cd,0x7f7f,0x4000,0x7f80,0x11004,1<<13,10),
             ('underflow',0x1cd,1,0x3f00,0,0x14000,1<<15,11),
             ('inexact',0x1ce,0x3f80,0x3b80,0x3f80,0x10000,1<<17,12),
             ('dot-snan',0x1c9,0xff81,0x3f80,0xffc10000,0x104,1<<9,8),
             ('dot-overflow',0x1c9,0x7f7f,0x4000,0x7f800000,0x11004,1<<13,10),
             ('dot-underflow',0x1c9,1,1,0,0x14000,1<<15,11),
             ('dot-inexact',0x1c9,0x3f80,0x3380,0x3f800000,0x10000,1<<17,12),
             ('lane-invalid-overflow',0x1cd,0x7f7f7f81,0x40003f80,0x7f807fc1,0x11104,(1<<9)|(1<<13)|(1<<17),8),
             ('lane-overflow-underflow',0x1cd,0x7f7f0001,0x40003f00,0x7f800000,0x15004,(1<<13)|(1<<15)|(1<<17),10)]
    for label,n,x,y,result,flags,enable,reason in vectors:
        for priority in [False,True] if flags&4 and reason!=7 else [False]:
            control=enable|(8 if priority else 0); etype=7 if priority else reason
            setup=op(0xc2)+'\n'+integer(20)+ptr(21)+'ldnl 0\n'+integer(9)+ptr(8)+op(-7)+'\n'
            operands=ptr(0)+'ldnl 0\n'+ptr(2)+'ldnl 0\n'
            operation=op(n)+'\n'
            result_store=integer(5)
            if n==0x1c9:
                operands=load(3,'sn')+ldc(1)+'\n'+ptr(2)+ptr(0)
                result_store=store(5,'sn')
            if a.tvx_soft:
                operands=ptr(0)
                operation='call tvx_soft_exception\n'
                result_store=integer(5)
            code=setup+ldc(control)+'\n'+op(0xb7)+'\n'+operands+operation+result_store+op(0xb6)+'\n'+integer(19)
            code+=ldc(0)+'\n'+op(0xb7)+'\n'+ptr(20)+'ldnl 0\n'+op(-7)+'\n'
            add('tvx-trap-'+label+('-priority' if priority else ''),code,{0:x,2:y,3:0x3f800000 if label=='dot-inexact' else 0,18:n,10:0x80000000,11:0x80000000},{4:etype,5:result,6:control,7:1 if a.tvx_soft else (15 if n==0x1c9 else 7),19:flags},['tvx-exception','selth','tret'])
            cases[-1]['handler']='stl -3\nstl -4\nldl 0\n'+ldc(0xbffcc)+'\n'+op(0x46)+'\nstl -2\nldl -1\nadc 1\nstl -1\nldl 4\nstl 8\nldc 0\nstl 0\nldc 0\n'+op(-5)+'\n'
            if not a.tvx_soft:
                # Saved integer operands must be intact before retry.
                checks=''
                for slot,original,weight in [(9,'ldlp -8\n' if n==0x1c9 else 'ldl -6\n',2),(10,'ldlp -6\n' if n==0x1c9 else 'ldl -8\n',4)]+([(11,'ldc 1\n',8)] if n==0x1c9 else []):
                    checks+=f'ldl {slot}\n'+original+op(4)+'\neqc 0\n'+ldc(weight)+'\n'+op(0x53)+'\nldl -1\n'+op(5)+'\nstl -1\n'
                cases[-1]['handler']=cases[-1]['handler'].replace('ldl 4\nstl 8\n',checks+'ldl 4\nstl 8\n')
    # Clear only the delivered enable, requiring invalid/overflow/inexact
    # retries to drain in priority order, with no partial sticky publication.
    source=next(c for c in cases if c['name']=='tvx-trap-lane-invalid-overflow')
    add('tvx-trap-sequential-enables',source['code'],{i:v for i,v in enumerate(source['input']) if v},
        {4:12,5:0x7f807fc1,6:1<<17,7:3 if a.tvx_soft else 21,19:0x11104},source['ops'])
    clear_one='ldc 1\nldl -4\nldc 2\n'+op(0x53)+'\nadc -7\n'+op(0x41)+'\n'+op(0x32)+'\nldl 0\n'+op(0x46)+'\nstl 0\n'
    cases[-1]['handler']=source['handler'].replace('ldc 0\nstl 0\n',clear_one)
    # Sticky flags survive a later operation; fptesterr clears only FPError.
    for label,n,x,y,result,flags,enable,reason in [vectors[3],vectors[6],vectors[8]]:
        operands=ptr(0)+'ldnl 0\n'+ptr(2)+'ldnl 0\n'
        operation=op(n)+'\n'; result_store=integer(5)
        if n==0x1c9:
            operands=load(3,'sn')+ldc(1)+'\n'+ptr(2)+ptr(0)
            result_store=store(5,'sn')
        if a.tvx_soft:
            operands=ptr(0); operation='call tvx_soft_exception\n'; result_store=integer(5)
        code=ldc(0x400)+'\n'+op(0xb7)+'\n'+operands+operation+result_store
        code+=op(0xb6)+'\n'+integer(6)+op(0x9c)+'\n'+integer(7)+op(0xb6)+'\n'+integer(19)
        add('tvx-sticky-'+label,code,{0:x,2:y,18:n},{5:result,6:flags|0x400,7:0 if flags&4 else 1,19:(flags|0x400)&~4},['tvx-exception','ldflags','fptesterr'])
if a.tvx_protected:
    if not a.tvx_traps: raise ValueError('--tvx-protected requires --tvx-traps')
    cases=[]
    for label,n,x,y,result,flags,enable,reason in vectors:
        variants=[('specific',[reason],enable)]
        if flags&4 and reason!=7: variants.append(('priority',[7],enable|8))
        if label=='lane-invalid-overflow': variants.append(('sequential',[8,10,12],enable))
        for suffix,reasons,control in variants:
            # PDS io[8..18], identity RODS io[20], private workspace and FP image.
            enter=ptr(20)+'ldnl 0\n'+ptr(8)+op(-6)+'\n'
            code=ptr(21)+'ldnl 0\n'+integer(13)+ptr(0)+ldc(0x8003c004)+'\nstnl 0\n'
            code+=ldc(1)+'\n'+integer(7)+enter
            remaining=control|0x400
            for index,etype in enumerate(reasons):
                code+=integer(5)+integer(4)
                # All checks combine into io[7], expected to remain one.
                checks=[ptr(5)+'ldnl 0\neqc 0\n',ptr(4)+'ldnl 0\n'+f'eqc {etype}\n',
                        ptr(8)+'ldnl 0\n'+ldc(0xbffcc)+'\n'+op(0x46)+'\n'+ldc(remaining)+'\n'+op(4)+'\neqc 0\n',
                        ptr(8)+'ldnl 0\n'+ldc(1<<27)+'\n'+op(0x46)+'\n'+ldc(1<<27)+'\n'+op(4)+'\neqc 0\n']
                if not a.tvx_soft:
                    for slot,w in [(14,0),(15,2)]:
                        wanted=ptr(w) if n==0x1c9 else ptr(2 if slot==14 else 0)+'ldnl 0\n'
                        checks.append(ptr(slot)+'ldnl 0\n'+wanted+op(4)+'\neqc 0\n')
                    if n==0x1c9: checks.append(ptr(16)+'ldnl 0\neqc 1\n')
                for check in checks:code+=check+ptr(7)+'ldnl 0\n'+op(0x46)+'\n'+integer(7)
                # Supervisor owns FP context save/restore, not goprot itself.
                code+=ldc(0x8003b800)+'\n'+op(-1)+'\n'+op(0xa0)+'\n'+op(0xa0)+'\n'+op(0xa0)+'\n'+ldc(0x8003b800)+'\n'+op(-2)+'\n'
                if suffix=='sequential': remaining &= ~(1<<(2*etype-7))
                else: remaining &= ~0x2aa08
                code+=ldc(remaining)+'\n'+integer(8)+ptr(9)+'ldnl 0\n'+integer(13)+enter
            # Successful P continuation ends with a syscall (reason=8).
            code+=integer(1)+ptr(8)+'ldnl 0\n'+ldc(0xbffcc)+'\n'+op(0x46)+'\n'+integer(19)
            operands=ptr(0)+'ldnl 0\n'+ptr(2)+'ldnl 0\n';operation=op(n)+'\n';result_store=integer(5)
            if n==0x1c9:
                operands=load(3,'sn')+ldc(1)+'\n'+ptr(2)+ptr(0);result_store=store(5,'sn')
            if a.tvx_soft:
                operands=ptr(0);operation='call tvx_soft_exception\n';result_store=integer(5)
            add('tvx-p-'+label+'-'+suffix,code,{0:x,2:y,3:0x3f800000 if label=='dot-inexact' else 0,8:control|0x400,12:0x8003c000,18:n},
                {1:8,4:reasons[-1],5:result,6:flags|0x400,7:1,19:flags|0x400},['goprot','tvx-exception','fpstall','fpldall'])
            cases[-1]['handler']=operands+operation+result_store+op(0xb6)+'\n'+integer(6)+op(-8)+'\n'
assembly=['.section .text,"ax"']
handlers={}
for c in cases:
    name='probe_'+str(c['id']);reset=op(0x9c)+'\n'+special(0x22)+op(0xa0)+'\n'+op(0xa0)+'\n'+op(0xa0)+'\n'+op(0x29)+'\n'
    if a.cpu=='t9000': reset=ldc(0)+'\n'+op(0xb7)+'\n'+reset
    assembly += [f'.global {name}',name+':',reset+op(0x22)+'\n'+integer(22)+c['code']+op(0x22)+'\n'+integer(23)+'ret']
    if 'handler' in c: handlers.setdefault(c['handler'],[]).append(c['id'])
# Identical handlers share one body, retaining every public case label. This
# keeps the expanded suite below the fixed 256 KiB bootstrap workspace guard.
for body,ids in handlers.items():
    for i in ids: assembly += [f'.global handler_{i}',f'handler_{i}:']
    assembly.append(body)
(a.out/'probes.S').write_text('\n'.join(assembly)+'\n')
lines=['/* Generated; see generate.py for fixed bit-pattern oracles. */']
for c in cases:
    lines.append('extern void probe_%d(unsigned *);'%c['id'])
    if 'handler' in c: lines.append('extern void handler_%d(void);'%c['id'])
    values=[c['expected'][w] for w in sorted(c['expected'])]
    lines.append('static const unsigned want_%d[]={%s};'%(c['id'],','.join('0x%08xu'%v for v in values) or '0'))
    values=[v for v in c['input'][:20] if v]
    lines.append('static const unsigned input_%d[]={%s};'%(c['id'],','.join('0x%08xu'%v for v in values) or '0'))
lines.append('static const struct testcase tests[] = {')
for c in cases:
    assert all(0<=w<22 for w in c['expected']), 'timestamps are not fixed oracles'
    want=[0]*22;mask=0
    for w,v in c['expected'].items():want[w]=v;mask|=1<<w
    arr=lambda xs:'{'+','.join('0x%08xu'%x for x in xs)+'}'
    assert not any(c['input'][20:]), 'pointers and timestamps are initialized at runtime'
    input_mask=sum(1<<w for w,v in enumerate(c['input'][:20]) if v)
    lines.append('{"%s",probe_%d,input_%d,0x%x,want_%d,0x%x,%s},'%(c['name'],c['id'],c['id'],input_mask,c['id'],mask,('handler_'+str(c['id']) if 'handler' in c else '0')))
lines.append('};');(a.out/'cases.h').write_text('\n'.join(lines)+'\n')
(a.out/'coverage.json').write_text(json.dumps([{k:v for k,v in c.items() if k!='code'} for c in cases],indent=2))
# Consumers with staged trap support can classify these cases explicitly;
# the T4 runner still requires every case to pass, without expected failures.
(a.out/'trap-cases.json').write_text(json.dumps([{'id':c['id'],'name':c['name']} for c in cases if 'handler' in c],indent=2)+'\n')
print(a.cpu,len(cases),'cases')
