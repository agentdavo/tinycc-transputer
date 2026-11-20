#!/usr/bin/env python3
"""Validate arithmetic answers and timing against the independent manifest."""
import argparse,json,re
from pathlib import Path
def validate(info,text,unit):
 n=info['checks']
 rs=re.findall(r'^TVX-(OK|FAIL) ([0-9a-f]{8}) got=([0-9a-f]{8}) want=([0-9a-f]{8}) flags=([0-9a-f]{8}) wantflags=([0-9a-f]{8})$',text,re.M)
 ts=re.findall(r'^TVX-TIME ([0-9a-f]{8}) '+unit+r'=([0-9a-f]{8})$',text,re.M)
 ss=re.findall(r'^TVX-BF16 PASS checks=([0-9a-f]{8}) failed=00000000$',text,re.M)
 expected=[w for row in info['pairs'] for w in row[2:4]]+[d['want'] for d in info['dots'] for _ in range(2)]
 expected_flags=[w for row in info['pairs'] for w in row[4:6]]+[d['flags'] for d in info['dots'] for _ in range(2)]
 valid=len(rs)==len(ts)==n==len(expected) and len(ss)==1 and int(ss[0],16)==n
 for i,(status,ident,g,w,f,wf) in enumerate(rs):
  g,w=int(g,16),int(w,16)
  match=g==w and int(f,16)==int(wf,16) and i<len(expected_flags) and int(wf,16)==expected_flags[i]
  valid=valid and status=='OK' and int(ident,16)==i and i<len(expected) and w==expected[i] and match
 valid=valid and all(int(ident,16)==i and (unit=="ticks" or int(t,16)>0) for i,(ident,t) in enumerate(ts))
 return dict(checks=n,timing_records=len(ts),passed=valid)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('reference',type=Path);p.add_argument('uart',type=Path);p.add_argument('--unit',default='instructions',choices=['instructions','ticks','t800_model_cycles']);a=p.parse_args()
 result=validate(json.loads(a.reference.read_text()),a.uart.read_text(errors='replace'),a.unit);print(json.dumps(result,indent=2));raise SystemExit(0 if result['passed'] else 1)
