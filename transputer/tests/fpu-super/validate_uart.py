#!/usr/bin/env python3
"""Reject truncated UART logs and report real test failures without whitelisting."""
import argparse,json,re,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('coverage',type=Path);p.add_argument('uart',type=Path);p.add_argument('--unit',choices=['ticks','instructions','t800_model_cycles'],default='ticks');a=p.parse_args()
cases=json.loads(a.coverage.read_text());text=a.uart.read_text(errors='replace')
begin=re.findall(r'^BEGIN ([0-9a-f]{8}) (.+)$',text,re.M)
results=re.findall(r'^(PASS|FAIL) (\S+) bits=(.*)$',text,re.M)
summary=re.findall(r'^FPU-SUPER (PASS|FAIL) tests=([0-9a-f]{8}) failed=([0-9a-f]{8})$',text,re.M)
complete=(len(begin)==len(results)==len(cases) and len(summary)==1)
if complete:
 complete=all(int(b[0],16)==c['id'] and b[1]==c['name'] and r[1]==c['name'] for c,b,r in zip(cases,begin,results))
if complete:
 for c,r in zip(cases,results):
  try:
   observed=[int(x,16) for x in r[2].split()]
   expected=[v for k,v in sorted(c['expected'].items(),key=lambda kv:int(kv[0]))]
   if len(observed)!=len(expected) or (r[0]=='PASS')!=(observed==expected):complete=False
  except ValueError:complete=False
timing=re.findall(r'^TIME ([0-9a-f]{8}) (ticks|instructions|t800_model_cycles)=([0-9a-f]{8})$',text,re.M)
complete=complete and len(timing)==len(cases) and all(int(t[0],16)==c['id'] and t[1]==a.unit for c,t in zip(cases,timing))
failed=[r[1] for r in results if r[0]=='FAIL']
if complete:
 s=summary[0];complete=int(s[1],16)==len(cases) and int(s[2],16)==len(failed) and s[0]==('FAIL' if failed else 'PASS')
bf16=None
manifest=a.coverage.parent/'bf16-manifest.json'
if manifest.exists():
 info=json.loads(manifest.read_text()); count=info['widen_patterns']+info['narrow_vectors']*(5 if info.get('memory_paths') else 1)
 bs=re.findall(r'^BF16 PASS checks=([0-9a-f]{8}) failed=00000000$',text,re.M)
 bt=re.findall(r'^TIME (widen|narrow|indirect|packed1|packed2|packed3) ([0-9a-f]{8}) '+a.unit+r'=([0-9a-f]{8})$',text,re.M)
 sequence=[(op,offset) for op,n in [('widen',info['widen_patterns']),('narrow',info['narrow_vectors'])] for offset in range(0,n,256)]
 if info.get('memory_paths'):
  sequence=[('widen',offset) for offset in range(0,info['widen_patterns'],256)]+[(op,offset) for offset in range(0,info['narrow_vectors'],256) for op in ['narrow','indirect','packed1','packed2','packed3']]
 observed=[(op,int(offset,16)) for op,offset,_ in bt]
 valid=len(bs)==1 and int(bs[0],16)==count and observed==sequence and not re.search(r'^FAIL input=',text,re.M) and len(re.findall(r'^FPU-ALL PASS$',text,re.M))==1
 complete=complete and valid
 bf16=dict(checks=count,timing_records=len(bt),passed=valid)
tvx=None
manifest=a.coverage.parent/'tvx-bf16-reference.json'
if manifest.exists():
 from validate_tvx_bf16 import validate
 tvx=validate(json.loads(manifest.read_text()),text,a.unit)
 complete=complete and tvx['passed']
print(json.dumps(dict(complete=complete,cases=len(cases),results=len(results),timing_unit=a.unit,timing_records=len(timing),failed=failed,bf16=bf16,tvx=tvx),indent=2))
sys.exit(2 if not complete else (1 if failed else 0))
