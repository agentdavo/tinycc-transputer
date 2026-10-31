import struct, sys
FIX={0:1,1:1,2:2,3:2,4:4,5:4,6:4,7:1,10:8,11:8,12:8}
FMT={0:'<B',1:'<b',2:'<H',3:'<h',4:'<I',5:'<i',6:'<f',7:'<B',10:'<Q',11:'<q',12:'<d'}
path=sys.argv[1]; want=sys.argv[2]; nblk=int(sys.argv[3])
f=open(path,'rb'); assert f.read(4)==b'GGUF'
ver,=struct.unpack('<I',f.read(4)); nt,=struct.unpack('<Q',f.read(8)); nkv,=struct.unpack('<Q',f.read(8))
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
    if k=='general.alignment':
        align=struct.unpack(FMT[t],f.read(FIX[t]))[0]
    else: sk(t)
tensors=[]
for _ in range(nt):
    nm=rs(); nd,=struct.unpack('<I',f.read(4))
    dims=[struct.unpack('<Q',f.read(8))[0] for _ in range(nd)]
    tt,=struct.unpack('<I',f.read(4)); off,=struct.unpack('<Q',f.read(8))
    tensors.append((nm,dims,tt,off))
pos=f.tell()
data_start=(pos+align-1)//align*align
for nm,dims,tt,off in tensors:
    if nm==want:
        print("found %s dims=%s type=%d offset=%d data_start=%d"%(nm,dims,tt,off,data_start),file=sys.stderr)
        f.seek(data_start+off)
        blob=f.read(nblk*17)
        sys.stdout.buffer.write(blob)
        print("wrote %d blocks (%d bytes)"%(nblk,len(blob)),file=sys.stderr)
        break
else:
    print("not found; first few:", [t[0] for t in tensors[:5]], file=sys.stderr)
