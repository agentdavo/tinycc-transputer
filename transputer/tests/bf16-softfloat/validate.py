#!/usr/bin/env python3
import hashlib,json,re,struct,sys
from pathlib import Path
out=Path(sys.argv[1]); ref=json.loads((out/'reference/reference.json').read_text())
binary=ref.get('format')=='f16'
memory=(out/'memory-paths').exists()
labels='widen|narrow|indirect|packed1|packed2|packed3' if memory else 'widen|narrow'
rows=[]
for path in ['software','tvx']:
    for part,start in enumerate(range(0,len(ref['narrow']),2048)):
        directory=out/f'{path}-{part}'
        count=min(2048,len(ref['narrow'])-start)*(5 if memory else 1)+(65536 if part==0 else 0)
        timings=[]
        for fmt in ['direct','link']:
            log=(directory/f'{fmt}.uart').read_text()
            summary=re.findall(('F16' if binary else 'BF16')+r' PASS checks=([0-9a-f]{8}) failed=00000000',log)
            assert len(summary)==1 and int(summary[0],16)==count and 'FAIL' not in log,(path,part,fmt,log[-2000:])
            records=re.findall(r'^TIME ('+labels+r') ([0-9a-f]{8}) instructions=([0-9a-f]{8})$',log,re.M)
            expected=(256 if part==0 else 0)+(min(2048,len(ref['narrow'])-start)+255)//256*(5 if memory else 1)
            sequence=[("widen",offset) for offset in range(0,65536,256)] if part==0 else []
            sequence += [(op,offset) for offset in range(0,min(2048,len(ref["narrow"])-start),256) for op in labels.split("|")[1:]]
            assert len(records)==expected and [(op,int(offset,16)) for op,offset,_ in records]==sequence and all(int(r[2],16)>0 for r in records)
            timings.append(records)
            rows.append(dict(path=path,part=part,format=fmt,checks=count,timing_records=len(records),instructions=sum(int(r[2],16) for r in records),sha256=hashlib.sha256((directory/f'{fmt}.btl').read_bytes()).hexdigest()))
        assert timings[0]==timings[1]
        # Decode executable symbols to prove the TVX/software split. Ignore
        # arbitrary data bytes; only inspect widen and narrow function bodies.
        elf=(directory/'test.elf').read_bytes()
        phoff=struct.unpack_from('<I',elf,28)[0]; stride,num=struct.unpack_from('<HH',elf,42)
        ph=[struct.unpack_from('<8I',elf,phoff+i*stride) for i in range(num)]
        syms=[]
        for line in (directory/'test.elf.tccsym').read_text().splitlines():
            fields=line.split()
            if len(fields)==3 and fields[1].lower()=='t': syms.append((int(fields[0],16),fields[2]))
        syms.sort()
        functions=[('widen',0x1d6 if binary else 0x1c0),('narrow',0x1d7 if binary else 0x1c1)]
        if memory: functions += [(name,0x1d7 if binary else 0x1c1) for name in ['narrow_indirect','narrow_packed1','narrow_packed2','narrow_packed3']]
        for name,wanted in functions:
            idx=next(i for i,s in enumerate(syms) if s[1]==name)
            low=syms[idx][0];high=syms[idx+1][0]
            seg=next(s for s in ph if s[0]==1 and s[2]<=low<s[2]+s[4])
            code=elf[seg[1]+low-seg[2]:seg[1]+high-seg[2]]
            opr=[]; operand=0
            for b in code:
                operand|=b&15
                if b>>4==2: operand=(operand<<4)&0xffffffff
                elif b>>4==6: operand=((~operand)<<4)&0xffffffff
                else:
                    if b>>4==15: opr.append(operand)
                    operand=0
            if path=='tvx':
                assert wanted in opr,(name,opr)
                # Both functions pass binary32 bits through workspace.
                # Neither needs an FP load/store roundtrip for conversion.
                assert 0x8e not in opr and 0x88 not in opr, ('redundant FPU roundtrip',name,opr)
            else: assert not any(0x1c0<=n<=0x1ff for n in opr),(name,opr)
totals={}
for path in ['software','tvx']:
    totals[path]={op:sum(int(m[1],16) for f in out.glob(path+'-*/direct.uart') for m in re.findall(r'^TIME ('+labels+r') [0-9a-f]+ instructions=([0-9a-f]+)$',f.read_text(),re.M) if m[0]==op) for op in labels.split('|')}
doc=dict(format=ref.get('format','bf16'),runs=rows,reference=ref['source_sha256'],widen_nan_policy_cases=ref['widen_nan_policy_cases'],narrow_vectors=len(ref['narrow']),rounding='nearest-even',instruction_totals=totals,flags='SoftFloat flags recorded; integer/TVX conversion paths do not implement SoftFloat exception flags')
(out/'validation.json').write_text(json.dumps(doc,indent=2)+'\n')
print(f'{len(rows)} complete image runs; {sum(r["checks"] for r in rows)} conversion checks PASS; emitted software/TVX split verified')
