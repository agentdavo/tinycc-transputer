#!/usr/bin/env python3
"""Check executable function ranges, never data bytes, for TVX selection."""
import argparse,struct
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('elf',type=Path);p.add_argument('--backend',choices=['software','tvx'],required=True);a=p.parse_args()
elf=a.elf.read_bytes();phoff=struct.unpack_from('<I',elf,28)[0];stride,num=struct.unpack_from('<HH',elf,42)
ph=[struct.unpack_from('<8I',elf,phoff+i*stride) for i in range(num)]
syms=sorted((int(f[0],16),f[2]) for line in Path(str(a.elf)+'.tccsym').read_text().splitlines() if len(f:=line.split())==3 and f[1].lower()=='t')
for name,wanted in [('tvx_test_add',0x1ce),('tvx_test_mul',0x1cd),('tvx_test_dot',0x1c9)]:
 i=next(i for i,v in enumerate(syms) if v[1]==name);lo=syms[i][0];hi=syms[i+1][0]
 seg=next(s for s in ph if s[0]==1 and s[2]<=lo<hi<=s[2]+s[4]);code=elf[seg[1]+lo-seg[2]:seg[1]+hi-seg[2]]
 opr=[];operand=0
 for b in code:
  operand|=b&15
  if b>>4==2:operand=(operand<<4)&0xffffffff
  elif b>>4==6:operand=((~operand)<<4)&0xffffffff
  else:
   if b>>4==15:opr.append(operand)
   operand=0
 if a.backend=='tvx':assert wanted in opr,(name,opr)
 else:assert not any(0x1c0<=v<=0x1ff for v in opr),(name,opr)
print(a.backend+': packed add, packed multiply and dot instruction selection PASS')
