#!/usr/bin/env python3
"""Wrap our chunked direct-load image in an actual executable link-0 loader.
Wire: one length byte, bootstrap bytes, then contiguous payload. No chunk bytes.
Bootstrap relocates to 0x8003f000, workspace 0x8003e000 (256 KiB minimum RAM).
"""
import argparse,json,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('direct',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
b=a.direct.read_bytes();assert b[0]==0;i=1;payload=bytearray()
while True:
 n=b[i];i+=1
 if not n:break
 assert i+n<=len(b);payload+=b[i:i+n];i+=n
# Reserve a full page below the protected probe workspace at 0x8003c000.
# Main workspace (0x8003e000) and relocated loader (0x8003f000) are higher.
payload_limit = 0x3b000 - 0x70
assert i==len(b) and len(payload)<=payload_limit, 'payload overlaps workspace guard'
# Fixed-width operands make bootstrap labels independent of prefix length.
def ins(op,n):
 n&=0xffffffff
 ds=[(n>>k)&15 for k in range(28,-1,-4)]
 if n&0x80000000:return [0x20|((~n>>28)&15)]+[0x20|((~n>>k)&15) for k in range(24,4,-4)]+[0x60|((~n>>4)&15),op|ds[-1]]
 return [0x20|x for x in ds[:-1]]+[op|ds[-1]]
def opr(n):
 if n<16:return [0xf0|n]
 return [0x20|(n>>4),0xf0|(n&15)]
code=[];labels={};patch=[]
def emit(bs):code.extend(bs)
def ldc(n):emit(ins(0x40,n))
def label(n):labels[n]=len(code)
def branch(op,n):patch.append((len(code),op,n));emit([0]*8)
def uart(ch):
 name='tx'+str(len(code));label(name);ldc(0x10000004);emit([0x30]);ldc(1);emit(opr(0x46));branch(0xa0,name)
 ldc(ch);ldc(0x10000000);emit([0xe0])
# Copy the entire bootstrap to high memory before overwriting its load address.
ldc(0x80000070);ldc(0x8003f000);size_patch=len(code);ldc(0);emit(opr(0x4a));jump_patch=len(code);ldc(0);emit(opr(6))
label('relocated');ldc(0x8003e000);emit(opr(0x3c));uart(ord('B'))
# IN: C=destination, B=input channel, A=byte count.
ldc(0x80000070);ldc(0x80000010);ldc(len(payload));emit(opr(7));uart(ord('L'))
ldc(0x80000070);emit(opr(6))
assert 2<=len(code)<=255,len(code)
code[size_patch:size_patch+8]=ins(0x40,len(code));code[jump_patch:jump_patch+8]=ins(0x40,0x8003f000+labels['relocated'])
for at,op,name in patch:code[at:at+8]=ins(op,labels[name]-(at+8))
a.output.write_bytes(bytes([len(code)])+bytes(code)+payload)
meta=dict(format='link0-bootstrap-then-raw-payload',bootstrap_bytes=len(code),payload_bytes=len(payload),load='0x80000070',workspace='0x8003e000',minimum_ram_bytes=262144,sha256=hashlib.sha256(a.output.read_bytes()).hexdigest())
a.output.with_suffix('.json').write_text(json.dumps(meta,indent=2));print(meta)
