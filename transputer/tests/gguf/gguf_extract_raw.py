import struct,sys
FIX={0:1,1:1,2:2,3:2,4:4,5:4,6:4,7:1,10:8,11:8,12:8}
FMT={0:'<B',1:'<b',2:'<H',3:'<h',4:'<I',5:'<i',6:'<f',7:'<B',10:'<Q',11:'<q',12:'<d'}
path,want,nbytes=sys.argv[1],sys.argv[2],int(sys.argv[3])
f=open(path,'rb'); f.read(4)
struct.unpack('<I',f.read(4)); nt,=struct.unpack('<Q',f.read(8)); nkv,=struct.unpack('<Q',f.read(8))
def rs():
    n,=struct.unpack('<Q',f.read(8)); return f.read(n).decode('utf8','replace')
def sk(t):
    if t==8: rs(); return
    if t==9:
        et,=struct.unpack('<I',f.read(4)); n,=struct.unpack('<Q',f.read(8))
        if et==8:
            for _ in range(n): rs()
        else: f.seek(FIX[et]*n,1)
        return
    f.seek(FIX[t],1)
align=32
for _ in range(nkv):
    k=rs(); t,=struct.unpack('<I',f.read(4))
    if k=='general.alignment': align=struct.unpack(FMT[t],f.read(FIX[t]))[0]
    else: sk(t)
ts=[]
for _ in range(nt):
    nm=rs(); nd,=struct.unpack('<I',f.read(4))
    dims=[struct.unpack('<Q',f.read(8))[0] for _ in range(nd)]
    tt,=struct.unpack('<I',f.read(4)); off,=struct.unpack('<Q',f.read(8))
    ts.append((nm,dims,tt,off))
ds=(f.tell()+align-1)//align*align
for nm,dims,tt,off in ts:
    if nm==want:
        sys.stderr.write("%s dims=%s type=%d\n"%(nm,dims,tt))
        f.seek(ds+off); sys.stdout.buffer.write(f.read(nbytes)); sys.exit(0)
sys.stderr.write("NOT FOUND %s\n"%want)
