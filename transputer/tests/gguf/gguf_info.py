import struct, sys, collections
GGML={0:'F32',1:'F16',2:'Q4_0',3:'Q4_1',6:'Q5_0',7:'Q5_1',8:'Q8_0',9:'Q8_1',10:'Q2_K',11:'Q3_K',
12:'Q4_K',13:'Q5_K',14:'Q6_K',15:'Q8_K',16:'IQ2_XXS',17:'IQ2_XS',18:'IQ3_XXS',19:'IQ1_S',20:'IQ4_NL',
21:'IQ3_S',22:'IQ2_S',23:'IQ4_XS',24:'I8',25:'I16',26:'I32',27:'I64',28:'F64',29:'IQ1_M',30:'BF16',
34:'TQ1_0',35:'TQ2_0',36:'IQ4_KS',37:'IQ2_KS',38:'IQ4_KSS',39:'MXFP4'}
FIX={0:1,1:1,2:2,3:2,4:4,5:4,6:4,7:1,10:8,11:8,12:8}
FMT={0:'<B',1:'<b',2:'<H',3:'<h',4:'<I',5:'<i',6:'<f',7:'<B',10:'<Q',11:'<q',12:'<d'}
f=open(sys.argv[1],'rb')
assert f.read(4)==b'GGUF'
ver,=struct.unpack('<I',f.read(4))
nt,=struct.unpack('<Q',f.read(8)); nkv,=struct.unpack('<Q',f.read(8))
def rs():
    n,=struct.unpack('<Q',f.read(8)); return f.read(n).decode('utf8','replace')
def rv(t):
    if t==8: return rs()
    if t==9:
        et,=struct.unpack('<I',f.read(4)); n,=struct.unpack('<Q',f.read(8))
        if et==8:
            out=[]
            for i in range(n):
                s=rs()
                if i<3: out.append(s)
            return out+['...(%d)'%n] if n>3 else out
        if et==9: raise RuntimeError("nested array")
        sz=FIX[et]; buf=f.read(sz*n)
        vals=list(struct.unpack('<%d%s'%(min(n,3),FMT[et][1]),buf[:sz*min(n,3)]))
        return vals+['...(%d)'%n] if n>3 else vals
    return struct.unpack(FMT[t],f.read(FIX[t]))[0]
kv={}
for _ in range(nkv):
    k=rs(); t,=struct.unpack('<I',f.read(4)); kv[k]=rv(t)
print("gguf v%d  tensors_in_shard=%d  kv=%d"%(ver,nt,nkv))
for k in sorted(kv):
    if k.startswith('tokenizer.ggml.') and k.split('.')[-1] in ('tokens','scores','token_type','merges'): continue
    print("  %-48s %s"%(k,str(kv[k])[:80]))
cnt=collections.Counter(); byt=collections.Counter(); ex={}
for _ in range(nt):
    nm=rs(); nd,=struct.unpack('<I',f.read(4))
    dims=[struct.unpack('<Q',f.read(8))[0] for _ in range(nd)]
    tt,=struct.unpack('<I',f.read(4)); off,=struct.unpack('<Q',f.read(8))
    n=1
    for d in dims: n*=d
    name=GGML.get(tt,'TYPE%d'%tt)
    cnt[name]+=n; byt[name]+=1
    ex.setdefault(name,(nm,dims))
print("\nQUANT DISTRIBUTION IN THIS SHARD (by element count):")
tot=sum(cnt.values()) or 1
for t,c in cnt.most_common():
    print("  %-9s %6d tensors  %14d elems  %5.1f%%   e.g. %s %s"%(t,byt[t],c,100.0*c/tot,ex[t][0][:42],ex[t][1]))
