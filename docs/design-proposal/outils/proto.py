"""Prototype sur image privee : jetons, palette du fond, polices. Aucun source touche."""
import struct, sys, os
sys.path.insert(0,'.')
import imgpatch, tokens
FD=os.environ.get("ARTIFAKT_FONTS", "")  # le dossier des polices Artifakt
PG=[0xD6DEE9,0xDCE3ED,0xD1DAE6,0xE1E7EF,0xCDD7E4]
PN=[0x1A1E26,0x1D222B,0x181C23,0x20252F,0x1B2029]
def rd(fs,p):
    pos,e,cl=fs.find(p); n=int.from_bytes(e[28:32],'little')
    d=b''
    for c in fs.chain(cl):
        fs.f.seek(fs.cl(c)); d+=fs.f.read(fs.cs)
    return d[:n]
def rep(b,old,new):
    i=b.find(old); assert i>=0; assert b.find(old,i+1)<0
    return b[:i]+new+b[i+len(old):]
def apply(fs,font=False):
    u=rd(fs,'/lib/uikit.so')
    p=lambda T:struct.pack('<24I',*T)
    u=rep(u,p(tokens.OLD_G),p(tokens.NEW_G)); u=rep(u,p(tokens.OLD_N),p(tokens.NEW_N))
    fs.write('/lib/uikit.so',u)
    v=rd(fs,'/sys/voronoy')
    q=lambda T:struct.pack('<5I',*T)
    v=rep(v,q([0x7FA6EC,0x5A8BE0,0xA9C2F0,0xC4CEDD,0x4B78CF]),q(PG))
    v=rep(v,q([0x1C2433,0x22314A,0x18202C,0x2B4266,0x202838]),q(PN))
    fs.write('/sys/voronoy',v)
    if font:
        fs.write('/fonts/DejaVuSans.ttf',open(FD+'Inter-Regular.ttf','rb').read())
        fs.write('/fonts/DejaVuSans-Bold.ttf',open(FD+'Inter-SemiBold.ttf','rb').read())
