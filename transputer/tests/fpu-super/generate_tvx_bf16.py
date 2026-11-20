#!/usr/bin/env python3
"""Generate arithmetic goldens using the supplied SoftFloat fork, not T4."""
import argparse,hashlib,json,random,shutil,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
files=['f32_add','f32_mul','s_addMagsF32','s_subMagsF32','s_normSubnormalF32Sig','s_roundPackToF32','s_normRoundPackToF32','s_propagateNaNF32UI','s_countLeadingZeros8','softfloat_state','softfloat_raiseFlags']
with tempfile.TemporaryDirectory() as tmp:
 w=Path(tmp)
 for f in a.source.glob('*.h'):shutil.copyfile(f,w/f.name)
 for f in files:shutil.copyfile(a.source/(f+'.c'),w/(f+'.c'))
 digest=hashlib.sha256(b''.join(f.name.encode()+f.read_bytes() for f in sorted(w.iterdir()))).hexdigest()
 subprocess.run(['gcc','-O2','-I'+str(w),str(Path(__file__).with_name('tvx_bf16_oracle.c')),*map(str,w.glob('*.c')),'-o',str(w/'oracle')],check=True)
 flags=[0]
 def calc(op,x,y):
  am=x&0x7fffffff;bm=y&0x7fffffff
  if am>=0x7f800000 or bm>=0x7f800000:flags[0]|=4
  if (am>0x7f800000 and not x&0x400000) or (bm>0x7f800000 and not y&0x400000):flags[0]|=0x104
  if am>0x7f800000:return x|0x400000
  if bm>0x7f800000:return y|0x400000
  r,exc=[int(v,16) for v in subprocess.check_output([str(w/'oracle')],input=f'{op} {x:x} {y:x}\n',text=True).split()]
  flags[0]|=sum(bit for sf,bit in [(16,0x104),(8,0x404),(4,0x1004),(2,0x4000),(1,0x10000)] if exc&sf)
  return r
 def narrow(x):
  if x&0x7fffffff>0x7f800000:return (x>>16)|64
  r=((x+0x7fff+((x>>16)&1))>>16)&65535
  if (x&0x7fffffff)<0x7f800000 and x&65535:
   flags[0]|=0x10000
   if r&0x7fff==0x7f80:flags[0]|=0x1004
   elif r&0x7f80==0:flags[0]|=0x4000
  return r
 edges=[0,0x8000,1,0x8001,0x7f,0x80,0x8080,0x3f00,0x3f80,0xbf80,0x3f81,0x3f82,0x3b80,0x7f7f,0xff7f,0x7f80,0xff80,0x7fc1,0xffc2,0x7f81]
 pairs=[(x|(edges[(i+3)%len(edges)]<<16),y|(edges[(j+7)%len(edges)]<<16)) for i,x in enumerate(edges) for j,y in enumerate(edges)]
 rng=random.Random(329);pairs += [(rng.getrandbits(32),rng.getrandbits(32)) for _ in range(128)]
 # Every bfloat16 NaN sign/payload, in both lanes and operand positions.
 nans=[sign|0x7f80|payload for sign in [0,0x8000] for payload in range(1,128)]
 for nan in nans:
  pairs += [(nan|0x3f800000,0x3f80|(nan<<16)),
            (nan|(nan<<16),0xffc2|0x7f810000)]
 rows=[]
 for x,y in pairs:
  wants=[];exceptions=[]
  for op in [0,1]:
   flags[0]=0
   wants.append(sum(narrow(calc(op,((x>>shift)&65535)<<16,((y>>shift)&65535)<<16))<<shift for shift in [0,16]))
   exceptions.append(flags[0])
  rows.append((x,y,*wants,*exceptions))
 dots=[([],[],0x80000000),([0x3f80],[0x4000],0x3f000000),([0x4b80,0x3f80,0xcb80],[0x3f80]*3,0x3f800000)]
 for i in range(len(edges)):dots.append(([edges[i],edges[(i+1)%len(edges)],0x3f80],[0x3f80,0xbf80,0x3f00],0x3e800000))
 for n in [1,2,3,7,8,16,31]:dots.append(([rng.getrandbits(16) for _ in range(n)],[rng.getrandbits(16) for _ in range(n)],0xbf000000))
 # Preserve low binary32 payload bits in the incoming dot accumulator,
 # including empty vectors, and establish final accumulator priority.
 for acc in [0x7f800001,0xff800123,0x7fc01234,0xffc05678]:
  dots += [([],[],acc),([0x3f80],[0x3f80],acc),
           ([0xff81],[0x7fc2],acc),([0x7f80],[0],acc)]
 for x,y in [(0x7f81,0xffc2),(0xffc2,0x7f81),(0xff81,0x7fc2),(0x7fc2,0xff81)]:
  dots += [([x],[y],0),([x,0xffc3],[y,0x3f80],0),
           ([0x7f80,x],[0,y],0)]
 dr=[]
 for xs,ys,acc in dots:
  total=0;flags[0]=0
  for x,y in zip(xs,ys):total=calc(0,total,calc(1,x<<16,y<<16))
  want=calc(0,acc,total)
  dr.append(dict(x=xs,y=ys,acc=acc,want=want,flags=flags[0]))
 h='/* Generated with SoftFloat. Exact TVX NaN sign/payload policy; finite results from SoftFloat. */\nstatic const unsigned pairs[][5]={\n'+''.join('{'+','.join(hex(v)+'u' for v in (*row[:4],(row[4]>>2)|((row[5]>>2)<<16)))+'},\n' for row in rows[:528])+'};\n'
 h+='#define PAIR_COUNT '+str(len(rows))+'u\n'
 h+='static const struct dotcase {unsigned n,acc,want,flags;unsigned short x[31],y[31];} dots[]={\n'
 for d in dr:h+='{'+f'{len(d["x"])},{hex(d["acc"])}u,{hex(d["want"])}u,{hex(d["flags"])}u,'+'{'+','.join(hex(v) for v in d['x'])+'},{'+','.join(hex(v) for v in d['y'])+'}},\n'
 h+='};\n';(a.out/'tvx-bf16-vectors.h').write_text(h)
 (a.out/'tvx-bf16-reference.json').write_text(json.dumps(dict(source_sha256=digest,header_sha256=hashlib.sha256(h.encode()).hexdigest(),pair_cases=len(rows),dot_cases=len(dr),seed=329,rounding='nearest-even',nan_policy='first NaN in logical operand order; preserve sign/payload, quiet; invalid without NaNs returns positive canonical NaN',nan_patterns=254,checks=2*len(rows)+2*len(dr),pairs=rows,dots=dr),indent=2)+'\n')
