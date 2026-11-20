#!/usr/bin/env python3
"""Audit T9000 FP inventory, probe presence and TinyCC mnemonic encodings.

Inventory transcribed from the local T9000 ISM chapters 11, A and B.
Chapter A/B OCR omits/misreads some entries; the project's later direct
encodings DD (fpadddbsn) and error-control instructions are explicit here.
This checks opcode coverage, not exhaustive IEEE behavioural conformance.
"""
import argparse
import json
import re
import struct
import subprocess
from pathlib import Path

ARCH = dict(
    fpldnldbi=0x82, fpstnldb=0x84, fpldnlsni=0x86, fpadd=0x87,
    fpstnlsn=0x88, fpsub=0x89, fpldnldb=0x8a, fpmul=0x8b,
    fpdiv=0x8c, fprange=0x8d, fpldnlsn=0x8e, fpnan=0x91,
    fpordered=0x92, fpnotfinite=0x93, fpgt=0x94, fpeq=0x95,
    fpi32tor32=0x96, fpge=0x97, fpi32tor64=0x98, fpb32tor64=0x9a,
    fplg=0x9b, fprtoi32=0x9d, fpstnli32=0x9e, fpldzerosn=0x9f,
    fpldzerodb=0xa0, fpint=0xa1, fpdup=0xa3, fprev=0xa4,
    fpldnladddb=0xa6, fpldnlmuldb=0xa8, fpldnladdsn=0xaa, fpldnlmulsn=0xac,
    fprem=0xcf, fprn=0xd0, fpdivby2=0xd1, fpmulby2=0xd2,
    fpsqrt=0xd3, fprp=0xd4, fprm=0xd5, fprz=0xd6,
    fpr32tor64=0xd7, fpr64tor32=0xd8, fpexpdec32=0xd9, fpexpinc32=0xda,
    fpabs=0xdb, fpadddbsn=0xdd, fpchki32=0xde, fpchki64=0xdf,
    fpstall=-1, fpldall=-2)
# Retained compatibility/diagnostic operations, deliberately audited too.
COMPAT = dict(fpsttest=0x80, fpchkerr=0x83, fpldtest=0x85,
              fptesterr=0x9c, fpseterr=0xcb, fpclrerr=0xdc)

def encode(n):
    if n < 0:
        return bytes([0x60 | ((~(n >> 4)) & 15), 0xf0 | (n & 15)])
    return bytes([0x20 | (n >> 4), 0xf0 | (n & 15)])

def text_section(path):
    data=path.read_bytes()
    assert data[:6]==b'\x7fELF\x01\x01'
    off=struct.unpack_from('<I',data,32)[0]
    size,count,names=struct.unpack_from('<HHH',data,46)
    sections=[struct.unpack_from('<10I',data,off+i*size) for i in range(count)]
    strings=sections[names]
    strings=data[strings[4]:strings[4]+strings[5]]
    section=next(s for s in sections if strings[s[0]:].split(b'\0',1)[0]==b'.text')
    return data[section[4]:section[4]+section[5]]

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--suite',type=Path,required=True)
    p.add_argument('--tcc',required=True)
    a=p.parse_args()
    cases=json.loads((a.suite/'coverage.json').read_text())
    source=(a.suite/'probes.S').read_text()
    rows=[]
    for name,opcode in {**ARCH,**COMPAT}.items():
        probes=[c['name'] for c in cases if name in c['ops']]
        assert probes, f'No named behavioural probe: {name}'
        raw='.byte '+','.join(hex(b) for b in encode(opcode))
        assert raw in source, f'No emitted opcode: {name}'
        rows.append(dict(name=name,opcode=opcode,bytes=encode(opcode).hex(),
                         category='architectural' if name in ARCH else 'compatibility/diagnostic',
                         probes=probes))
    assembly=a.suite/'mnemonic-audit.S'
    assembly.write_text('.text\n.global fp_opcode_audit\nfp_opcode_audit:\n'+
                        '\n'.join({**ARCH,**COMPAT})+'\n')
    obj=a.suite/'mnemonic-audit.o'
    subprocess.run([a.tcc,'-mt9000','-c',str(assembly),'-o',str(obj)],check=True)
    expected=b''.join(encode(n) for n in {**ARCH,**COMPAT}.values())
    actual=text_section(obj)
    assert actual==expected, f'Assembler byte mismatch: {actual.hex()} != {expected.hex()}'
    (a.suite/'opcode-audit.json').write_text(json.dumps(rows,indent=2)+'\n')
    print(f'{len(ARCH)} architectural + {len(COMPAT)} compatibility/diagnostic FP opcodes: probes and assembler bytes PASS')

if __name__=='__main__':
    main()
