"""Moteur des maquettes : dessine en x4 puis reduit (texte non hinte, comme ttf.asm),
aux pixels pres de l'ecran reel. Polices : celles que l'OS peut charger."""
import math, numpy as np, random, os, struct
from PIL import Image, ImageDraw, ImageFont, ImageFilter
SS=4
FD=os.environ.get("ARTIFAKT_FONTS", "")  # le dossier des polices Artifakt
DV=r"./sdcard/fonts/"
LS=r"$ONYX/pc/dist/Koton/res/fonts/"
FAM={'inter':{'r':FD+'Inter-Regular.ttf','b':FD+'Inter-SemiBold.ttf','m':DV+'DejaVuSansMono.ttf','s':LS+'LiberationSerif-Regular.ttf','sb':LS+'LiberationSerif-Bold.ttf'},
     'dejavu':{'r':DV+'DejaVuSans.ttf','b':DV+'DejaVuSans-Bold.ttf','m':DV+'DejaVuSansMono.ttf'}}
def rgb(c): return ((c>>16)&255,(c>>8)&255,c&255)
_fc={}
class C:
    def __init__(s,w,h,fam='inter'):
        s.w,s.h,s.fam=w,h,fam
        s.im=Image.new('RGB',(w*SS,h*SS),(0,0,0))
        s.d=ImageDraw.Draw(s.im,'RGBA')
    def font(s,k,px):
        key=(s.fam,k,px)
        if key not in _fc: _fc[key]=ImageFont.truetype(FAM[s.fam][k],px*SS)
        return _fc[key]
    def fill(s,x,y,w,h,c,a=255):
        s.d.rectangle([x*SS,y*SS,(x+w)*SS-1,(y+h)*SS-1],fill=rgb(c)+(a,))
    def rr(s,x,y,w,h,r,c,a=255,outline=None,oa=255):
        if outline is not None:
            s.d.rounded_rectangle([x*SS,y*SS,(x+w)*SS-1,(y+h)*SS-1],r*SS,fill=rgb(outline)+(oa,))
            s.d.rounded_rectangle([(x+1)*SS,(y+1)*SS,(x+w-1)*SS-1,(y+h-1)*SS-1],max(0,r-1)*SS,fill=rgb(c)+(a,))
        else:
            s.d.rounded_rectangle([x*SS,y*SS,(x+w)*SS-1,(y+h)*SS-1],r*SS,fill=rgb(c)+(a,))
    def line(s,x,y,w,c,a=255): s.fill(x,y,w,1,c,a)
    def vline(s,x,y,h,c,a=255): s.fill(x,y,1,h,c,a)
    def shadow(s,x,y,w,h,r,blur,dy,a):
        m=Image.new('L',(s.w,s.h),0)
        ImageDraw.Draw(m).rounded_rectangle([x,y+dy,x+w-1,y+dy+h-1],r,fill=int(a*255))
        m=m.filter(ImageFilter.GaussianBlur(blur/2)).resize(s.im.size,Image.BILINEAR)
        s.im.paste(Image.new('RGB',s.im.size,(0,0,0)),(0,0),m)
        s.d=ImageDraw.Draw(s.im,'RGBA')
    def tw(s,t,px,k='r'): return s.font(k,px).getlength(t)/SS
    def text(s,x,base,t,px,c,k='r',anchor='l',maxw=None):
        f=s.font(k,px)
        if maxw and s.tw(t,px,k)>maxw:
            while t and s.tw(t+'…',px,k)>maxw: t=t[:-1]
            t+='…'
        if anchor=='c': x-=s.tw(t,px,k)/2
        if anchor=='r': x-=s.tw(t,px,k)
        s.d.text((int(round(x))*SS,base*SS),t,font=f,fill=rgb(c)+(255,),anchor='ls')
    def poly(s,pts,c,w=1.5,closed=False,a=255):
        p=[(x*SS,y*SS) for x,y in pts]
        if closed:p.append(p[0])
        W=max(1,int(w*SS))
        s.d.line(p,fill=rgb(c)+(a,),width=W,joint='curve')
        for q in p: s.d.ellipse([q[0]-W/2,q[1]-W/2,q[0]+W/2,q[1]+W/2],fill=rgb(c)+(a,))
    def circ(s,x,y,r,c,w=None,fill=False,a=255):
        b=[(x-r)*SS,(y-r)*SS,(x+r)*SS,(y+r)*SS]
        if fill: s.d.ellipse(b,fill=rgb(c)+(a,))
        else: s.d.ellipse(b,outline=rgb(c)+(a,),width=max(1,int(w*SS)))
    def icon(s,name,x,y,sz,c,sw=None):
        ic=ICONS[name]; u=sz/24
        sw=sw or (1.5 if sz<=20 else sz/14)
        for it in ic:
            op=it[0]
            if op=='p': s.poly([(x+a*u,y+b*u) for a,b in it[1]],c,sw)
            elif op=='P': s.poly([(x+a*u,y+b*u) for a,b in it[1]],c,sw,True)
            elif op=='c': s.circ(x+it[1]*u,y+it[2]*u,it[3]*u,c,sw)
            elif op=='C': s.circ(x+it[1]*u,y+it[2]*u,it[3]*u,c,fill=True)
            elif op=='r':
                X,Y,W,H,R=it[1:]
                s.poly([(x+a*u,y+b*u) for a,b in rrpts(X,Y,W,H,R)],c,sw,True)
            elif op=='a':
                X,Y,R,A0,A1=it[1:]
                pts=[(X+R*math.cos(math.radians(t)),Y+R*math.sin(math.radians(t))) for t in np.linspace(A0,A1,24)]
                s.poly([(x+a*u,y+b*u) for a,b in pts],c,sw)
    def tile(s,col,name,x,y,sz,glyph=0xFFFFFF):
        r=round(sz*.24)
        s.rr(x,y,sz,sz,r,col)
        g=round(sz*.62); o=(sz-g)/2
        s.icon(name,x+o,y+o,g,glyph,max(1.5,sz/22))
    def real(s,name,x,y,sz):
        a=load_rgba(name,sz)
        im=Image.fromarray(np.clip(a,0,255).astype('uint8'),'RGBA').resize((sz*SS,sz*SS),Image.NEAREST)
        # (alpha premultiplie : on compose a la main)
        box=(int(x*SS),int(y*SS),int(x*SS)+sz*SS,int(y*SS)+sz*SS)
        bg=np.array(s.im.crop(box),float); ar=np.array(im,float)
        out=ar[...,:3]+bg*(1-ar[...,3:4]/255)
        s.im.paste(Image.fromarray(np.clip(out,0,255).astype('uint8')),box[:2])
        s.d=ImageDraw.Draw(s.im,'RGBA')
    def glyph(s,name,x,y,sz,c):
        a=load_rgba('glyph-'+name,sz)
        m=Image.fromarray(a[...,3].astype('uint8'),'L').resize((sz*SS,sz*SS),Image.NEAREST)
        s.im.paste(Image.new('RGB',m.size,rgb(c)),(int(x*SS),int(y*SS)),m)
        s.d=ImageDraw.Draw(s.im,'RGBA')
    def out(s,path): s.im.resize((s.w,s.h),Image.BOX).save(path,optimize=True)
ICDIR=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','ic')
def load_rgba(name,want):
    d=open(os.path.join(ICDIR,name+'.rgba'),'rb').read()
    n=struct.unpack('<H',d[6:8])[0]
    t=[struct.unpack('<HHI',d[8+i*8:16+i*8]) for i in range(n)]
    s,_,o=min(t,key=lambda e:abs(e[0]-want))
    a=np.frombuffer(d,dtype='<u4',count=s*s,offset=o).reshape(s,s)
    arr=np.dstack([(a>>16)&255,(a>>8)&255,a&255,(a>>24)&255]).astype(float)
    if s!=want:
        arr=np.array(Image.fromarray(arr.astype('uint8'),'RGBA').resize((want,want),Image.BILINEAR),float)
    return arr
def rrpts(x,y,w,h,r):
    p=[]
    for cx,cy,a0 in ((x+w-r,y+r,-90),(x+w-r,y+h-r,0),(x+r,y+h-r,90),(x+r,y+r,180)):
        for t in np.linspace(a0,a0+90,7):
            p.append((cx+r*math.cos(math.radians(t)),cy+r*math.sin(math.radians(t))))
    return p
_R=math.radians
ICONS={
'grid':[('C',x,y,1.8) for y in (5,12,19) for x in (5,12,19)],
'folder':[('P',[(3,6.5),(9.5,6.5),(11.5,9),(21,9),(21,19),(3,19)])],
'file':[('P',[(6,3),(14,3),(19,8),(19,21),(6,21)]),('p',[(14,3),(14,8),(19,8)])],
'doc':[('P',[(6,3),(14,3),(19,8),(19,21),(6,21)]),('p',[(14,3),(14,8),(19,8)]),('p',[(9,13),(16,13)]),('p',[(9,17),(16,17)])],
'term':[('r',3,4.5,18,15,2.5),('p',[(7,9),(10.5,12),(7,15)]),('p',[(12.5,15),(17,15)])],
'edit':[('p',[(4,20),(4.8,16),(16,4.8),(19.2,8),(8,19.2),(4,20)]),('p',[(14,7),(17,10)])],
'calc':[('r',5,3,14,18,2.5),('p',[(8,7.5),(16,7.5)])]+[('C',x,y,.9) for y in (12.5,16.5) for x in (8.5,12,15.5)],
'globe':[('c',12,12,9),('p',[(3,12),(21,12)]),('p',[(12,3),(9,8),(8.3,12),(9,16),(12,21),(15,16),(15.7,12),(15,8),(12,3)])],
'pulse':[('p',[(3,12),(8,12),(10.5,6),(13.5,18),(16,12),(21,12)])],
'gear':[('c',12,12,3),('c',12,12,7.5)]+[('p',[(12+7.5*math.cos(_R(a)),12+7.5*math.sin(_R(a))),(12+10*math.cos(_R(a)),12+10*math.sin(_R(a)))]) for a in range(0,360,45)],
'trash':[('p',[(4,7),(20,7)]),('p',[(9,7),(9,4),(15,4),(15,7)]),('P',[(6,7),(7,21),(17,21),(18,7)]),('p',[(10,11),(10,17)]),('p',[(14,11),(14,17)])],
'clip':[('r',6,4.5,12,16.5,2),('r',9,3,6,3.5,1)],
'power':[('p',[(12,3),(12,12)]),('a',12,13,8,-50,230)],
'search':[('c',10.5,10.5,6.5),('p',[(15.5,15.5),(21,21)])],
'sound':[('P',[(4,9.5),(8,9.5),(13,5),(13,19),(8,14.5),(4,14.5)]),('a',13,12,4.5,-50,50),('a',13,12,8,-50,50)],
'net':[('r',9,3,6,5,1),('r',3,16,6,5,1),('r',15,16,6,5,1),('p',[(12,8),(12,12)]),('p',[(6,16),(6,12),(18,12),(18,16)])],
'wifi':[('a',12,20,16,-125,-55),('a',12,20,10.5,-125,-55),('a',12,20,5,-125,-55),('C',12,20,1.2)],
'back':[('p',[(20,12),(4,12)]),('p',[(10,6),(4,12),(10,18)])],
'fwd':[('p',[(4,12),(20,12)]),('p',[(14,6),(20,12),(14,18)])],
'up':[('p',[(12,20),(12,4)]),('p',[(6,10),(12,4),(18,10)])],
'chev':[('p',[(9,6),(15,12),(9,18)])],
'down':[('p',[(6,9),(12,15),(18,9)])],
'list':[('p',[(8,6),(21,6)]),('p',[(8,12),(21,12)]),('p',[(8,18),(21,18)]),('C',4,6,1.2),('C',4,12,1.2),('C',4,18,1.2)],
'tiles':[('r',4,4,7,7,1.5),('r',13,4,7,7,1.5),('r',4,13,7,7,1.5),('r',13,13,7,7,1.5)],
'theme':[('c',12,12,9),('p',[(12,3),(12,21)]),('p',[(12,8),(19,8)]),('p',[(12,12),(21,12)]),('p',[(12,16),(19,16)])],
'kbd':[('r',2.5,6,19,12,2),('p',[(7,14.5),(17,14.5)])]+[('C',x,10,.9) for x in (7,10.3,13.6,17)],
'screen':[('r',3,4,18,12.5,2),('p',[(12,16.5),(12,20)]),('p',[(8,20.5),(16,20.5)])],
'panel':[('r',3,4,18,16,2),('p',[(3,15.5),(21,15.5)]),('C',7,18,.8)],
'chip':[('r',6,6,12,12,2),('r',10,10,4,4,.6)]+[('p',[(a,2.5),(a,6)]) for a in (9,15)]+[('p',[(a,18),(a,21.5)]) for a in (9,15)]+[('p',[(2.5,a),(6,a)]) for a in (9,15)]+[('p',[(18,a),(21.5,a)]) for a in (9,15)],
'dotnet':[('p',[(4,17),(4,7),(10,17),(10,7)]),('p',[(20,7),(14,7),(14,17),(20,17)]),('p',[(14,12),(19,12)])],
'pkg':[('P',[(12,3),(20,7.5),(20,16.5),(12,21),(4,16.5),(4,7.5)]),('p',[(4,7.5),(12,12),(20,7.5)]),('p',[(12,12),(12,21)])],
'home':[('p',[(3,11),(12,3.5),(21,11)]),('p',[(6,9),(6,20),(18,20),(18,9)])],
'music':[('p',[(9,18),(9,5),(20,3),(20,16)]),('c',6.5,18,2.5),('c',17.5,16,2.5)],
'image':[('r',3,4.5,18,15,2),('C',8.5,9.5,1.4),('p',[(3,16),(9,12),(14,16),(17,13.5),(21,17)])],
'video':[('r',3,5,18,14,2),('P',[(10,9),(15,12),(10,15)])],
'star':[('P',[(12,3.5),(14.6,9),(20.5,9.7),(16.2,13.8),(17.3,19.8),(12,16.9),(6.7,19.8),(7.8,13.8),(3.5,9.7),(9.4,9)])],
'disk':[('r',3,5,18,14,2),('p',[(3,13),(21,13)]),('C',17,16,.9)],
'ram':[('r',3,7,18,10,1.5),('p',[(7,17),(7,20)]),('p',[(12,17),(12,20)]),('p',[(17,17),(17,20)]),('p',[(8,10.5),(8,13.5)]),('p',[(12,10.5),(12,13.5)]),('p',[(16,10.5),(16,13.5)])],
'mail':[('r',3,5,18,14,2),('p',[(3.5,7),(12,13),(20.5,7)])],
'cal':[('r',3.5,5,17,16,2),('p',[(3.5,10),(20.5,10)]),('p',[(8,3),(8,7)]),('p',[(16,3),(16,7)])],
'note':[('P',[(5,4),(19,4),(19,15),(14,20),(5,20)]),('p',[(14,20),(14,15),(19,15)])],
'target':[('c',12,12,9),('c',12,12,5),('C',12,12,1.5)],
'pad':[('r',2.5,7,19,11,5),('p',[(6,12.5),(10,12.5)]),('p',[(8,10.5),(8,14.5)]),('C',15,14,1),('C',18,11,1)],
'cards':[('r',4,6,11,15,2),('p',[(9,6),(9.8,3.5),(20,5.5),(17,20),(15,19.6)])],
'clock':[('c',12,12,9),('p',[(12,6.5),(12,12),(16,14)])],
'chat':[('P',[(3,5),(21,5),(21,17),(10,17),(6,21),(6,17),(3,17)])],
'circuit':[('p',[(3,12),(7,12)]),('P',[(7,8),(12,8),(15,12),(12,16),(7,16)]),('p',[(15,12),(21,12)])],
'rocket':[('P',[(12,2.5),(15.5,8),(15.5,16),(8.5,16),(8.5,8)]),('p',[(8.5,12),(5,17),(8.5,17)]),('p',[(15.5,12),(19,17),(15.5,17)]),('p',[(12,18),(12,21.5)])],
'paint':[('c',12,12,9),('C',8,10,1.2),('C',12,7.5,1.2),('C',16,10,1.2),('C',9,15,1.2)],
'bars':[('p',[(5,20),(5,12)]),('p',[(12,20),(12,5)]),('p',[(19,20),(19,9)])],
'sheet':[('r',3.5,4,17,16,2),('p',[(3.5,10),(20.5,10)]),('p',[(3.5,15),(20.5,15)]),('p',[(10,4),(10,20)])],
'bold':[('p',[(7,5),(7,19),(14,19),(16.5,16.5),(16.5,14.5),(14,12),(7,12)]),('p',[(7,5),(13,5),(15.5,7.5),(15.5,9.5),(13,12)])],
'ital':[('p',[(10,5),(17,5)]),('p',[(7,19),(14,19)]),('p',[(13.5,5),(10.5,19)])],
'under':[('p',[(7,4),(7,12),(9.5,15),(14.5,15),(17,12),(17,4)]),('p',[(6,20),(18,20)])],
'al':[('p',[(4,6),(20,6)]),('p',[(4,10.5),(14,10.5)]),('p',[(4,15),(20,15)]),('p',[(4,19.5),(14,19.5)])],
'ac':[('p',[(4,6),(20,6)]),('p',[(7,10.5),(17,10.5)]),('p',[(4,15),(20,15)]),('p',[(7,19.5),(17,19.5)])],
'ar':[('p',[(4,6),(20,6)]),('p',[(10,10.5),(20,10.5)]),('p',[(4,15),(20,15)]),('p',[(10,19.5),(20,19.5)])],
'new':[('P',[(6,3),(14,3),(19,8),(19,21),(6,21)]),('p',[(14,3),(14,8),(19,8)]),('p',[(12.5,11),(12.5,17)]),('p',[(9.5,14),(15.5,14)])],
'open':[('p',[(3,18.5),(3,6.5),(9.5,6.5),(11.5,9),(19,9),(19,11.5)]),('P',[(3,18.5),(6.5,11.5),(21.5,11.5),(18,18.5)])],
'save':[('P',[(4,4),(16,4),(20,8),(20,20),(4,20)]),('p',[(8,20),(8,13.5),(16,13.5),(16,20)]),('p',[(8,4),(8,8.5),(14,8.5)])],
'undo':[('p',[(4,9),(9,4)]),('p',[(4,9),(9,14)]),('p',[(4,9),(15,9),(20,14),(15,19),(9,19)])],
'redo':[('p',[(20,9),(15,4)]),('p',[(20,9),(15,14)]),('p',[(20,9),(9,9),(4,14),(9,19),(15,19)])],
'print':[('p',[(7,8),(7,3.5),(17,3.5),(17,8)]),('r',3.5,8,17,9,2),('r',7,14,10,6.5,.5)],
'table':[('r',3.5,4.5,17,15,1.5),('p',[(3.5,9.5),(20.5,9.5)]),('p',[(3.5,14.5),(20.5,14.5)]),('p',[(12,4.5),(12,19.5)])],
'img2':[('r',3.5,4.5,17,15,2),('p',[(3.5,16),(9,11),(14,15.5),(20.5,10)])],
'plus':[('p',[(12,5),(12,19)]),('p',[(5,12),(19,12)])],
'minus':[('p',[(5,12),(19,12)])],
'max':[('r',5.5,5.5,13,13,1.5)],
'close':[('p',[(6,6),(18,18)]),('p',[(18,6),(6,18)])],
'check':[('p',[(5,12.5),(10,17.5),(19,7)])],
}
def voronoi(w,h,pal,sombre=False,edge=True,seed=7,cell=28000):
    random.seed(seed)
    n=max(8,round(w*h/cell)); mn=.62*math.sqrt(w*h/n); g=[]
    while len(g)<n:
        for _ in range(200):
            p=(random.uniform(-20,w+20),random.uniform(-20,h+20))
            if all((p[0]-a)**2+(p[1]-b)**2>mn*mn for a,b in g): break
        g.append(p)
    gs=np.array(g); Y,X=np.mgrid[0:h,0:w]
    d=np.stack([(X-a)**2+(Y-b)**2 for a,b in gs],0)
    i=np.argsort(d,0)[:2]
    col=np.array([rgb(pal[random.randrange(5)]) for _ in g],float)
    c=col[i[0]]
    t=(136-30*Y/h) if not sombre else (128-24*Y/h)
    t=t[...,None]
    c=np.where(t<128,c*t/128,c+(255-c)*(t-128)/127)
    if edge:
        d1=np.take_along_axis(d,i[0][None],0)[0]; d2=np.take_along_axis(d,i[1][None],0)[0]
        dist=(d2-d1)/(2*np.maximum(1,np.hypot(gs[i[1],0]-gs[i[0],0],gs[i[1],1]-gs[i[0],1])))
        m=(dist<1)[...,None]
        c=np.where(m,c+(255-c)*.08,c)
    return Image.fromarray(np.clip(c,0,255).astype('uint8'))
