"""Maquettes de la proposition : direction A (Givre calme) et B (Instrument)."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__)); sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'q'))
from mk import *
import tokens
BLUE,TEAL,GREEN,AMBER,ORANGE,RED,VIOLET,SLATE=0x2F6FDE,0x0F8F8C,0x2C9C62,0xD9912B,0xD9622B,0xD6404A,0x6C5BD0,0x56606E
def theme(n):
    T=dict(zip(tokens.N,tokens.NEW_G if n=='g' else tokens.NEW_N))
    T['dark']=n=='n'
    T['pal']=[0xD6DEE9,0xDCE3ED,0xD1DAE6,0xE1E7EF,0xCDD7E4] if n=='g' else [0x1A1E26,0x1D222B,0x181C23,0x20252F,0x1B2029]
    T['sh']=(.16,.10) if n=='g' else (.42,.30)
    T['hover']=0xE9ECF1 if n=='g' else 0x23272E
    T['bar']=0xF9FAFB if n=='g' else 0x1A1D22
    T['panel']=0xF9FAFB if n=='g' else 0x1A1D22
    T['ink']=0xFFFFFF
    T['accent_txt']=T['ACCENT'] if n=='g' else T['ACCENT_VIF']
    return T
PINS=['files','terminal','editor','calculator','jet','taskman']
def wall(c,T):
    im=voronoi(c.w,c.h,T['pal'],T['dark'],edge=False,seed=11).resize(c.im.size,Image.NEAREST)
    c.im.paste(im); c.d=ImageDraw.Draw(c.im,'RGBA')
def menubar(c,T,app=None,menus=()):
    c.fill(0,0,c.w,28,T['bar']); c.line(0,27,c.w,T['LIGNE'])
    c.d.polygon([(14*SS,8*SS),(20*SS,14*SS),(14*SS,20*SS),(8*SS,14*SS)],fill=rgb(T['ACCENT'] if not T['dark'] else T['ACCENT_VIF']))
    x=30
    if app:
        c.text(x,19,app,13,T['TEXTE1'],'b'); x+=c.tw(app,13,'b')+18
    for m in menus:
        c.text(x,19,m,13,T['TEXTE2']); x+=c.tw(m,13)+16
    c.text(c.w-12,19,'23:51',13,T['TEXTE1'],anchor='r')
    c.icon('net',c.w-72,6,16,T['TEXTE2']); c.icon('sound',c.w-98,6,16,T['TEXTE2'])
def panel(c,T,active=None,extra=None,ws=0,grid_on=False):
    y=c.h-48
    c.fill(0,y,c.w,48,T['panel']); c.line(0,y,c.w,T['LIGNE'])
    if grid_on: c.rr(8,y+8,32,32,8,T['ACCENT_CLAIR'])
    c.glyph('apps',14,y+14,20,T['TEXTE1'] if not grid_on else T['accent_txt'])
    c.vline(48,y+14,20,T['LIGNE'])
    p=list(PINS)+([extra] if extra else [])
    x=58
    for i,nm in enumerate(p):
        c.real(nm,x,y+8,32)
        if i==active or (extra and i==len(p)-1): c.rr(x+10,y+43,12,3,1,T['accent_txt'])
        x+=40
    x=c.w-8-34
    c.glyph('power',x+7,y+14,20,T['TEXTE2']); x-=34
    c.glyph('gear',x+7,y+14,20,T['TEXTE2']); x-=34
    c.glyph('clipboard',x+7,y+14,20,T['TEXTE2']); x-=34
    c.icon('trash',x+7,y+14,20,T['TEXTE2']); x-=10
    c.vline(x,y+14,20,T['LIGNE']); x-=8+4*26
    c.rr(x,y+11,4*26,26,8,T['CREUX'])
    for i in range(4):
        if i==ws: c.rr(x+i*26+2,y+13,22,22,6,T['SURFACE'] if not T['dark'] else T['ELEVE'],outline=T['LIGNE'])
        c.text(x+i*26+13,y+29,str(i+1),12,T['TEXTE1'] if i==ws else T['TEXTE3'],'b' if i==ws else 'r',anchor='c')
def window(c,T,x,y,w,h,body=None,active=True):
    a,b=T['sh']
    if active:
        c.shadow(x,y,w,h,10,14,4,a); c.shadow(x,y,w,h,10,3,1,b)
    else:
        c.shadow(x,y,w,h,10,6,2,a*.6)
    c.rr(x-1,y-1,w+2,h+2,11,T['LIGNE_FORTE'] if not T['dark'] else 0x3A3F49)
    c.rr(x,y,w,h,10,body if body is not None else T['FACE'])
def caps(c,T,x,y,w,active=True,light=False):
    col=T['TEXTE2'] if active else T['TEXTE3']
    if light: col=0xE6E8EC
    X=x+w-30
    c.icon('close',X+6,y+11,14,col,1.5)
    c.icon('max',X-22,y+11,14,col,1.5)
    c.icon('minus',X-50,y+11,14,col,1.5)
def side(c,T,x,y,w,h,col):
    c.rr(x,y,w+12,h,10,col); c.fill(x+w,y,14,h,T['SURFACE'])
def label(c,T,x,base,t): c.text(x,base,t,12,T['TEXTE3'],'b')
def button(c,T,x,y,w,h,t,main=False):
    if main:
        c.rr(x,y,w,h,8,T['ACCENT']); c.text(x+w/2,y+h/2+5,t,13,0xFFFFFF,'b',anchor='c')
    else:
        c.rr(x,y,w,h,8,T['ELEVE'],outline=T['LIGNE_FORTE']); c.text(x+w/2,y+h/2+5,t,13,T['TEXTE1'],anchor='c')
def field(c,T,x,y,w,h,t,chev=True,muted=False):
    c.rr(x,y,w,h,8,T['SURFACE'] if not T['dark'] else T['ELEVE'],outline=T['LIGNE_FORTE'])
    c.text(x+10,y+h/2+5,t,13,T['TEXTE3'] if muted else T['TEXTE1'],maxw=w-30)
    if chev: c.icon('down',x+w-22,y+h/2-7,14,T['TEXTE2'])
# --- ecrans A
def calcul(c,T,x,y):
    w,h=256,356
    window(c,T,x,y,w,h); caps(c,T,x,y,w)
    c.text(x+w-18,y+84,'1 234,5',32,T['TEXTE1'],anchor='r')
    keys=[['C','⌫','±','÷'],['7','8','9','×'],['4','5','6','−'],['1','2','3','+'],['0',None,',','=']]
    kw,kh,g=52,44,6
    X0=x+(w-4*kw-3*g)//2; Y0=y+104
    for r,row in enumerate(keys):
        for k,t in enumerate(row):
            if t is None: continue
            X=X0+k*(kw+g); Y=Y0+r*(kh+g); W=kw*2+g if t=='0' else kw
            if k==3 and r==4: c.rr(X,Y,W,kh,10,T['ACCENT']); col=0xFFFFFF
            elif k==3: c.rr(X,Y,W,kh,10,T['ACCENT_CLAIR']); col=T['accent_txt']
            elif r==0: c.rr(X,Y,W,kh,10,T['CREUX']); col=T['TEXTE1']
            else: c.rr(X,Y,W,kh,10,T['SURFACE'] if not T['dark'] else T['ELEVE'],outline=T['LIGNE']); col=T['TEXTE1']
            c.text(X+W/2,Y+28,t,16,col,anchor='c')
def desk(c,T):
    wall(c,T)
    # carte agenda : opaque, seulement quand elle a quelque chose a dire
    x=c.w-232; c.shadow(x,40,220,76,10,10,2,T['sh'][0]*.7)
    c.rr(x,40,220,76,10,T['FACE'],outline=T['LIGNE'])
    c.text(x+14,62,'Lundi 2 novembre',13,T['TEXTE1'],'b')
    c.text(x+14,80,'10:30  Revue du bureau',12,T['TEXTE2'])
    c.text(x+14,98,'17:00  Carte SD du Pi',12,T['TEXTE2'])
    calcul(c,T,44,64)
    menubar(c,T,'Calculatrice')
    panel(c,T,active=3)
APPS=[('2048','2048'),('Arkanoid','arkanoid'),('Bienvenue','welcome'),('Calculatrice','calculator'),('Calendrier','calendar'),
('Cardfile','cardfile'),('Cibles','targets'),('Circuits','circuits'),('Cosmo Zoom!','cosmozoom'),('Courier','courier'),
('Dessin','paint'),('Fichiers','files'),('FM Tracker','fmtracker'),('FreeCell','freecell'),('Game Boy','gbemu'),
('Game Library','gamelib'),('GPIO Lab','gpiolab'),('Horloge','clock'),('Invaders','invaders'),('IRC','irc')]
CATS=['Toutes','Accessoires','Bureautique','Création','Développement','Internet','Jeux','Multimédia','Système']
def launcher(c,T):
    wall(c,T); c.fill(0,0,c.w,c.h,0x10141A,90 if not T['dark'] else 120)
    menubar(c,T); panel(c,T,grid_on=True)
    x,y,w,h=8,36,784,508
    c.shadow(x,y,w,h,14,18,6,T['sh'][0]*1.2)
    c.rr(x-1,y-1,w+2,h+2,15,T['LIGNE_FORTE'] if not T['dark'] else 0x3A3F49)
    c.rr(x,y,w,h,14,T['FACE'])
    # rail des categories
    c.rr(x+8,y+8,160,h-16,10,T['CREUX'])
    for i,t in enumerate(CATS):
        Y=y+16+i*34
        if i==0: c.rr(x+14,Y,148,30,8,T['SURFACE'] if not T['dark'] else T['ELEVE'])
        c.text(x+26,Y+20,t,13,T['TEXTE1'] if i==0 else T['TEXTE2'],'b' if i==0 else 'r')
    # recherche
    X=x+180; W=w-180-16
    c.rr(X,y+12,W,38,10,T['SURFACE'] if not T['dark'] else T['ELEVE'],outline=T['LIGNE_FORTE'])
    c.icon('search',X+12,y+22,18,T['TEXTE3'])
    c.text(X+40,y+36,'Rechercher une application',13,T['TEXTE3'])
    cols=5; cw=W//cols; rh=94
    for i,(n,ic) in enumerate(APPS[:20]):
        cx=X+i%cols*cw; cy=y+66+(i//cols)*rh
        if i==7: c.rr(cx+6,cy-2,cw-12,88,10,T['hover'])
        c.real(ic,cx+(cw-48)//2,cy+4,48)
        c.text(cx+cw/2,cy+70,n,12,T['TEXTE1'],anchor='c',maxw=cw-12)
def reglages(c,T):
    wall(c,T); menubar(c,T,'Réglages'); panel(c,T,extra='control')
    x,y,w,h=12,36,776,506
    window(c,T,x,y,w,h,T['SURFACE']); sw=184
    side(c,T,x,y,sw,h,T['CREUX'])
    c.vline(x+sw,y,h,T['LIGNE'])
    items=[('Thème','theme'),('Région','clavier'),('Écran','ecran'),('Panel','dockconf'),('Wi-Fi','wifi'),('Son','sound'),('Préchargés','preload'),('.NET','dotnet'),('Logiciels','pkgman')]
    for i,(t,ic) in enumerate(items):
        Y=y+14+i*38
        if i==1:
            c.rr(x+8,Y,sw-16,34,8,T['ACCENT_CLAIR'])
        c.real(ic,x+16,Y+7,20)
        c.text(x+46,Y+22,t,13,T['accent_txt'] if i==1 else T['TEXTE1'],'b' if i==1 else 'r')
    caps(c,T,x,y,w)
    X=x+sw+28; W=w-sw-56
    c.text(X,y+48,'Région et langue',20,T['TEXTE1'],'b')
    c.text(X,y+68,'Clavier, langue et heure de cet appareil.',12,T['TEXTE2'])
    rows=[('Disposition du clavier','FR'),('Langue','Français'),('Pays (Wi-Fi)','France'),('Fuseau horaire (NTP)','Europe/Paris')]
    Y=y+88
    c.rr(X,Y,W,len(rows)*48,10,T['FACE'],outline=T['LIGNE'])
    for i,(a,b) in enumerate(rows):
        R=Y+i*48
        if i: c.line(X+14,R,W-14,T['LIGNE'])
        c.text(X+14,R+29,a,13,T['TEXTE1'])
        field(c,T,X+W-204,R+9,194,30,b)
    c.text(X+2,Y+len(rows)*48+24,'La traduction est progressive : certaines applications restent en anglais.',12,T['TEXTE3'])
    button(c,T,x+w-20-104,y+h-48,104,32,'Appliquer',True)
    button(c,T,x+w-20-104-8-88,y+h-48,88,32,'Annuler')
def flat_folder(c,x,y,sz,T):
    u=sz/48; col=0xE3A93C; top=0xEDBE63
    c.rr(x+3*u,y+8*u,18*u,10*u,3*u,col)
    c.rr(x+3*u,y+13*u,42*u,28*u,4*u,col)
    c.rr(x+3*u,y+17*u,42*u,24*u,4*u,top)
def flat_file(c,x,y,sz,T):
    u=sz/48
    fg=T['SURFACE'] if not T['dark'] else 0x2C3038
    c.rr(x+9*u,y+4*u,30*u,40*u,3*u,fg,outline=T['LIGNE_FORTE'])
    for i in range(3): c.fill(int(x+15*u),int(y+(18+7*i)*u),int(18*u),1,T['LIGNE_FORTE'])
def fichiers(c,T):
    wall(c,T); menubar(c,T,'Fichiers',['Fichier','Aller','Affichage','Volume']); panel(c,T,active=0)
    x,y,w,h=12,36,776,506
    window(c,T,x,y,w,h,T['SURFACE']); sw=176
    side(c,T,x,y,sw,h,T['CREUX']); c.vline(x+sw,y,h,T['LIGNE'])
    Y=y+24; label(c,T,x+16,Y,'Accès rapides')
    for i,(t,ic) in enumerate([('Applications','grid'),('Documents','doc'),('Musique','music'),('Images','image'),('Vidéos','video')]):
        R=y+34+i*30
        c.icon(ic,x+16,R+7,16,T['TEXTE2']); c.text(x+42,R+20,t,13,T['TEXTE1'])
    Y=y+34+5*30+22; label(c,T,x+16,Y,'Volumes')
    for i,(t,s,ic) in enumerate([('SD0:','Carte SD','disk'),('SD1:','UserFS','disk'),('RAM0:','Mémoire','ram'),('IMG0:','Image','disk')]):
        R=Y+10+i*38
        if i==0: c.rr(x+8,R,sw-16,36,8,T['ACCENT_CLAIR'])
        c.icon(ic,x+16,R+10,16,T['accent_txt'] if i==0 else T['TEXTE2'])
        c.text(x+42,R+16,t,13,T['TEXTE1'],'b' if i==0 else 'r'); c.text(x+42,R+30,s,12,T['TEXTE2'] if i==0 else T['TEXTE3'])
    X=x+sw
    c.line(X+1,y+44,w-sw-2,T['LIGNE'])
    bx=X+12
    for ic,on in (('back',1),('fwd',0),('up',1)):
        c.icon(ic,bx+5,y+13,18,T['TEXTE1'] if on else T['TEXTE3']); bx+=30
    bx+=6
    c.text(bx,y+27,'SD0:',13,T['TEXTE2']); bx+=c.tw('SD0:',13)+6
    c.icon('chev',bx,y+15,14,T['TEXTE3']); bx+=20
    c.text(bx,y+27,'apps',13,T['TEXTE1'],'b')
    tx=x+w-128-34
    c.rr(tx-30,y+9,58,28,8,T['CREUX'])
    c.rr(tx-28,y+11,26,24,6,T['SURFACE'] if not T['dark'] else T['ELEVE']); c.icon('tiles',tx-23,y+15,16,T['TEXTE1']); c.icon('list',tx+5,y+15,16,T['TEXTE3'])
    caps(c,T,x,y,w)
    names=['apps','basic','bin','docs','dotnet','etc','firmware','fonts','gcc','home','lib','res','src','sys','t','var','webkit','README.TXT','test.img','test_exfat.img']
    cw=(w-sw-24)//6; cy=y+56
    for i,n in enumerate(names):
        cx=X+12+i%6*cw; R=cy+(i//6)*94
        if i==5:
            c.rr(cx+4,R,cw-8,88,10,T['ACCENT_CLAIR'])
        if i<17: flat_folder(c,cx+(cw-48)//2,R+6,48,T)
        else: flat_file(c,cx+(cw-48)//2,R+6,48,T)
        c.text(cx+cw/2,R+74,n,12,T['TEXTE1'],anchor='c',maxw=cw-10)
    c.line(X+1,y+h-26,w-sw-2,T['LIGNE'])
    c.text(X+14,y+h-8,'20 éléments  ·  etc sélectionné',12,T['TEXTE2'])
def letters(c,T):
    wall(c,T); menubar(c,T,'Letters',['Fichier','Édition','Affichage','Insertion','Format','Tableau','Outils']); panel(c,T,extra='letters')
    x,y,w,h=8,34,784,510
    window(c,T,x,y,w,h,T['FACE'])
    gc=T['TEXTE2']
    X=x+12; Y=y+7
    def ib(ic,on=False,dis=False):
        nonlocal X
        if on: c.rr(X,Y,30,30,8,T['ACCENT_CLAIR'])
        c.icon(ic,X+6,Y+6,18,T['accent_txt'] if on else (T['TEXTE3'] if dis else gc)); X+=32
    def sep():
        nonlocal X
        c.vline(X+4,Y+6,18,T['LIGNE']); X+=11
    for i in ('new','open','save','print'): ib(i)
    sep()
    ib('undo'); ib('redo',dis=True); sep()
    for i in ('table','img2'): ib(i)
    sep()
    c.rr(X,Y+2,76,26,8,T['CREUX']); c.icon('minus',X+6,Y+9,12,gc); c.text(X+38,Y+20,'92 %',12,T['TEXTE1'],anchor='c'); c.icon('plus',X+58,Y+9,12,gc)
    caps(c,T,x,y,w)
    Y=y+42; c.line(x,Y-1,w,T['LIGNE'])
    X=x+12; Y+=2
    field(c,T,X,Y+1,108,28,'Normal'); X+=114
    field(c,T,X,Y+1,142,28,'Liberation Serif'); X+=148
    field(c,T,X,Y+1,54,28,'12'); X+=62
    ib('bold'); ib('ital'); ib('under'); sep()
    ib('al',True); ib('ac'); ib('ar'); sep()
    c.rr(X+4,Y+6,18,18,5,T['TEXTE1']); c.rr(X+4,Y+25,18,3,1,0xD6404A); X+=32
    Y=y+80; c.line(x,Y,w,T['LIGNE'])
    # regle
    c.fill(x,Y+1,w,19,T['FACE'])
    px0=x+70; pw=w-140
    c.fill(px0,Y+1,pw,19,T['SURFACE'] if not T['dark'] else 0x20242B)
    for i in range(0,20):
        tx=px0+i*pw/19
        c.fill(int(tx),Y+13,1,3,T['LIGNE_FORTE'])
        if i and i%2==0: c.text(tx,Y+11,str(i),9 if False else 10,T['TEXTE3'],anchor='c')
    Y+=20; c.line(x,Y,w,T['LIGNE'])
    H=y+h-24-Y-1
    c.fill(x,Y+1,w,H,T['CREUX'])
    py=Y+14; c.shadow(px0,py,pw,500,0,8,2,.12)
    c.fill(px0,py,pw,y+h-24-py,0xFFFFFF)
    ink=0x1B1F24; px=px0+58; S=DV+'DejaVuSerif.ttf'
    c.fam2=None
    tt=[('Rapport de démarrage',22,'b'),]
    c.text(px,py+64,'Rapport de démarrage',24,ink,'sb')
    lines=['Le système atteint le bureau en moins de six secondes sur le','PocketTerm. Le cache de fatfs réduit la lecture des kits, et le','préchargement rend l\'ouverture de Jet immédiate.','','Cette note reprend les mesures de novembre, avec la carte de','128 Go, sous Givre et sous Onyx Noir.']
    for i,l in enumerate(lines): c.text(px,py+100+i*22,l,15,0x2A2E34,'s')
    c.fill(x,y+h-24,w,1,T['LIGNE'])
    c.rr(x,y+h-24,w,24,10,T['FACE']); c.fill(x,y+h-24,w,8,T['FACE']); c.line(x,y+h-24,w,T['LIGNE'])
    c.text(x+14,y+h-8,'Page 1 sur 1',12,T['TEXTE2']); c.text(x+110,y+h-8,'74 mots',12,T['TEXTE2'])
    c.text(x+w-14,y+h-8,'Français',12,T['TEXTE2'],anchor='r')
# --- direction B : Instrument
def themeB(n):
    if n=='g':
        return dict(bg=0xE8EBEF,dots=0xC9CFD8,face=0xF7F8FA,surf=0xFFFFFF,side=0xEFF1F4,line=0xC7CDD6,ink=0x101317,ink2=0x444C58,ink3=0x626B78,acc=0xC2410C,accink=0xFFFFFF,accsoft=0xFBE9E0,dark=False)
    return dict(bg=0x0C0D0F,dots=0x24272D,face=0x131518,surf=0x16181C,side=0x0F1113,line=0x2E323A,ink=0xF0F1F3,ink2=0xB4B9C2,ink3=0x8B919B,acc=0xFF7A33,accink=0x16100C,accsoft=0x33200F,dark=True)
def b_shell(c,B,app,menus):
    c.fill(0,0,c.w,c.h,B['bg'])
    for yy in range(40,c.h-30,16):
        for xx in range(8,c.w,16): c.fill(xx,yy,1,1,B['dots'])
    c.fill(0,0,c.w,24,B['face']); c.line(0,24,c.w,B['line'])
    x=10; c.text(x,17,'ARMPI',12,B['acc'],'m'); x+=50
    c.text(x,17,app,12,B['ink'],'b'); x+=c.tw(app,12,'b')+16
    for m in menus: c.text(x,17,m,12,B['ink2']); x+=c.tw(m,12)+14
    c.text(c.w-10,17,'23:51',12,B['ink'],'m',anchor='r')
    c.icon('net',c.w-62,5,14,B['ink2']); c.icon('sound',c.w-84,5,14,B['ink2'])
    y=c.h-36
    c.fill(0,y,c.w,36,B['face']); c.line(0,y,c.w,B['line'])
    c.icon('grid',12,y+10,16,B['ink']); c.vline(38,y,36,B['line'])
    return y
def b_tasks(c,B,y,items,active):
    x=38
    for i,(ic,t) in enumerate(items):
        W=int(c.tw(t,12))+40
        if i==active: c.fill(x+1,y+1,W-1,35,B['surf']); c.fill(x+1,y,W-1,2,B['acc'])
        c.icon(ic,x+10,y+11,14,B['ink'] if i==active else B['ink2'])
        c.text(x+30,y+23,t,12,B['ink'] if i==active else B['ink2']); x+=W; c.vline(x,y,36,B['line'])
    c.text(c.w-142,y+23,'1 2 3 4',12,B['ink3'],'m')
    c.fill(c.w-143,y+29,8,2,B['acc'])
    c.icon('trash',c.w-86,y+11,14,B['ink2']); c.icon('gear',c.w-60,y+11,14,B['ink2']); c.icon('power',c.w-34,y+11,14,B['ink2'])
def b_win(c,B,x,y,w,h,col=None):
    c.fill(x-1,y-1,w+2,h+2,B['ink2'] if not B['dark'] else 0x4A505C)
    c.fill(x,y,w,h,col if col is not None else B['face'])
def b_caps(c,B,x,y,w):
    for i,ic in enumerate(('close','max','minus')):
        c.icon(ic,x+w-22-i*24,y+9,12,B['ink2'],1.25)
def b_desk(c,B):
    y=b_shell(c,B,'Calculatrice',[])
    b_tasks(c,B,y,[('calc','Calculatrice'),('term','Terminal')],0)
    x,Y,w,h=40,56,256,352
    b_win(c,B,x,Y,w,h); b_caps(c,B,x,Y,w)
    c.text(x+w-16,Y+78,'1 234,5',32,B['ink'],'m',anchor='r')
    keys=[['C','⌫','±','÷'],['7','8','9','×'],['4','5','6','−'],['1','2','3','+'],['0',None,',','=']]
    kw=(w-1)//4; kh=48; Y0=Y+h-5*kh
    c.line(x,Y0-1,w,B['line'])
    for r,row in enumerate(keys):
        for k,t in enumerate(row):
            if t is None: continue
            X=x+k*kw; yy=Y0+r*kh; W=2*kw if t=='0' else kw
            if t=='=': c.fill(X,yy,w-3*kw,kh,B['acc']); col=B['accink']
            else: col=B['acc'] if not B['dark'] and k==3 else (B['acc'] if k==3 else B['ink'])
            c.text(X+W/2,yy+30,t,16,col,'m',anchor='c')
            if k: c.vline(X,yy,kh,B['line'])
        if r: c.line(x,Y0+r*kh,w,B['line'])
    tx=c.w-250
    b_win(c,B,tx,Y,226,120,B['surf'])
    c.text(tx+12,Y+22,'LUN 02 NOV',12,B['ink3'],'m')
    c.text(tx+12,Y+58,'23:51',32,B['ink'],'m')
    c.line(tx,Y+74,226,B['line'])
    c.text(tx+12,Y+96,'10:30  Revue du bureau',12,B['ink2'])
def b_reg(c,B):
    y=b_shell(c,B,'Réglages',[])
    b_tasks(c,B,y,[('gear','Réglages'),('term','Terminal')],0)
    x,Y,w,h=10,32,780,524
    b_win(c,B,x,Y,w,h,B['surf']); sw=168
    c.fill(x,Y,sw,h,B['side']); c.vline(x+sw,Y,h,B['line'])
    for i,(t,ic) in enumerate([('Thème','theme'),('Région','kbd'),('Écran','screen'),('Panel','panel'),('Wi-Fi','wifi'),('Son','sound'),('Préchargés','chip'),('.NET','dotnet'),('Logiciels','pkg')]):
        R=Y+8+i*32
        if i==1: c.fill(x+1,R,sw-1,32,B['surf']); c.fill(x,R,2,32,B['acc'])
        c.icon(ic,x+14,R+8,16,B['ink'] if i==1 else B['ink2']); c.text(x+40,R+21,t,13,B['ink'] if i==1 else B['ink2'],'b' if i==1 else 'r')
    b_caps(c,B,x,Y,w)
    X=x+sw+20; W=w-sw-40
    c.text(X,Y+40,'Région et langue',20,B['ink'],'b')
    Yr=Y+64; c.line(X,Yr,W,B['line'])
    for i,(a,v) in enumerate([('Disposition du clavier','FR'),('Langue','Français'),('Pays (Wi-Fi)','France'),('Fuseau horaire (NTP)','Europe/Paris')]):
        R=Yr+i*44
        c.text(X,R+27,a,13,B['ink']); c.text(X+W-18,R+27,v,13,B['ink'],'m',anchor='r'); c.icon('chev',X+W-14,R+16,12,B['ink3'])
        c.line(X,R+44,W,B['line'])
    c.fill(x+w-20-100,Y+h-44,100,28,B['acc']); c.text(x+w-20-50,Y+h-25,'Appliquer',13,B['accink'],'b',anchor='c')
def b_fich(c,B):
    y=b_shell(c,B,'Fichiers',['Fichier','Aller','Affichage','Volume'])
    b_tasks(c,B,y,[('folder','Fichiers'),('term','Terminal')],0)
    x,Y,w,h=10,32,780,524
    b_win(c,B,x,Y,w,h,B['surf']); sw=156
    c.fill(x,Y,sw,h,B['side']); c.vline(x+sw,Y,h,B['line'])
    c.text(x+12,Y+22,'VOLUMES',12,B['ink3'],'m')
    for i,(t,s) in enumerate([('SD0:','Carte SD'),('SD1:','UserFS'),('RAM0:','Mémoire'),('IMG0:','Image')]):
        R=Y+30+i*28
        if i==0: c.fill(x+1,R,sw-1,28,B['surf']); c.fill(x,R,2,28,B['acc'])
        c.text(x+12,R+19,t,12,B['ink'],'m'); c.text(x+sw-10,R+19,s,12,B['ink3'],anchor='r')
    c.text(x+12,Y+174,'LIEUX',12,B['ink3'],'m')
    for i,t in enumerate(['Applications','Documents','Musique','Images','Vidéos']):
        c.text(x+12,Y+198+i*26,t,13,B['ink2'])
    X=x+sw; b_caps(c,B,x,Y,w)
    c.text(X+14,Y+22,'SD0:/',13,B['ink'],'m')
    c.line(X+1,Y+33,w-sw-1,B['line'])
    c.fill(X+1,Y+34,w-sw-1,24,B['side'])
    c.text(X+14,Y+50,'Nom',12,B['ink3'],'b'); c.text(X+w-sw-90,Y+50,'Taille',12,B['ink3'],'b',anchor='r'); c.text(X+w-sw-14,Y+50,'Type',12,B['ink3'],'b',anchor='r')
    c.line(X+1,Y+58,w-sw-1,B['line'])
    rows=[('apps','—','Dossier'),('basic','—','Dossier'),('bin','—','Dossier'),('docs','—','Dossier'),('dotnet','—','Dossier'),('etc','—','Dossier'),('firmware','—','Dossier'),('fonts','—','Dossier'),('gcc','—','Dossier'),('home','—','Dossier'),('lib','—','Dossier'),('res','—','Dossier'),('src','—','Dossier'),('sys','—','Dossier'),('README.TXT','2 Ko','Texte'),('test.img','8 Mo','Image')]
    for i,(a,b,t) in enumerate(rows):
        R=Y+59+i*27
        if i==5: c.fill(X+1,R,w-sw-1,27,B['acc']); fg=B['accink']; f2=B['accink']
        else: fg=B['ink']; f2=B['ink3']
        c.icon('folder' if t=='Dossier' else 'file',X+14,R+6,15,fg if i==5 else B['ink2'],1.25)
        c.text(X+38,R+19,a,13,fg); c.text(X+w-sw-90,R+19,b,12,f2,'m',anchor='r'); c.text(X+w-sw-14,R+19,t,12,f2,anchor='r')
def board(c,T):
    c.fill(0,0,c.w,c.h,T['FACE'])
    S=T['SURFACE'] if not T['dark'] else T['ELEVE']
    def h(y,t): c.text(24,y,t,12,T['TEXTE3'],'b')
    h(28,'Boutons : repos, survol, appui, focus, désactivé')
    x=24
    for i,(lab,kw) in enumerate((('Repos',0),('Survol',1),('Appui',2),('Focus',3),('Grisé',4))):
        y=40
        if kw==3: c.rr(x-3,y-3,102,38,10,T['ACCENT'],140)
        if kw==4: c.rr(x,y,96,32,8,T['FACE'],outline=T['LIGNE']); c.text(x+48,y+21,lab,13,T['TEXTE3'],anchor='c')
        else:
            bg={0:T['ELEVE'],1:T['hover'],2:T['CREUX'],3:T['ELEVE']}[kw]
            c.rr(x,y,96,32,8,bg,outline=T['LIGNE_FORTE']); c.text(x+48,y+21,lab,13,T['TEXTE1'],anchor='c')
        y=82
        if kw==3: c.rr(x-3,y-3,102,38,10,T['ACCENT'],140)
        col={0:T['ACCENT'],1:T['ACCENT_FONCE'],2:T['ACCENT_FONCE'],3:T['ACCENT'],4:T['CREUX']}[kw]
        c.rr(x,y,96,32,8,col); c.text(x+48,y+21,lab,13,T['TEXTE3'] if kw==4 else 0xFFFFFF,'b',anchor='c')
        y=124
        if kw in(1,2): c.rr(x,y,96,32,8,T['hover'] if kw==1 else T['CREUX'])
        if kw==3: c.rr(x,y,96,32,8,T['ACCENT'],60)
        c.text(x+48,y+21,lab,13,T['TEXTE3'] if kw==4 else T['TEXTE1'],anchor='c')
        x+=108
    h(184,'Champs')
    c.rr(24,194,180,32,8,S,outline=T['LIGNE_FORTE']); c.text(34,215,'Rechercher…',13,T['TEXTE3'])
    c.rr(213,191,186,38,10,T['ACCENT'],140); c.rr(216,194,180,32,8,S,outline=T['ACCENT']); c.text(226,215,'pocketterm',13,T['TEXTE1']); c.fill(298,201,1,18,T['ACCENT'])
    field(c,T,408,194,150,32,'Français')
    h(252,'Choix')
    y=262
    c.rr(24,y,18,18,5,T['ACCENT']); c.icon('check',26,y+2,14,0xFFFFFF,2); c.text(50,y+14,'Case cochée',13,T['TEXTE1'])
    c.rr(152,y,18,18,5,S,outline=T['LIGNE_FORTE']); c.text(178,y+14,'Case',13,T['TEXTE1'])
    c.circ(239,y+9,9,T['ACCENT'],fill=True); c.circ(239,y+9,3.5,0xFFFFFF,fill=True); c.text(256,y+14,'Radio',13,T['TEXTE1'])
    c.rr(322,y,36,20,10,T['ACCENT']); c.circ(348,y+10,8,0xFFFFFF,fill=True)
    c.rr(370,y,36,20,10,T['LIGNE_FORTE']); c.circ(380,y+10,8,0xFFFFFF,fill=True)
    c.rr(426,y+8,130,4,2,T['CREUX']); c.rr(426,y+8,84,4,2,T['ACCENT']); c.circ(510,y+10,8,0xFFFFFF,fill=True); c.circ(510,y+10,8,T['LIGNE_FORTE'],1)
    h(312,'Liste : repos, survol, sélection, sélection sans focus')
    y=322
    c.rr(24,y,270,4*30+8,10,S,outline=T['LIGNE'])
    for i,(t,bg,fg) in enumerate((('kernel8.img',None,T['TEXTE1']),('uikit.so',T['hover'],T['TEXTE1']),('appkit.so',T['ACCENT'],0xFFFFFF),('netkit.so',T['ACCENT_CLAIR'],T['TEXTE1']))):
        R=y+4+i*30
        if bg is not None: c.rr(30,R,258,30,6,bg)
        c.icon('file',38,R+7,16,fg if bg==T['ACCENT'] else T['TEXTE2']); c.text(62,R+20,t,13,fg)
        c.text(280,R+20,'61 Ko',12,fg if bg==T['ACCENT'] else T['TEXTE3'],anchor='r')
    h(482,'Onglets')
    c.rr(24,492,270,34,10,T['CREUX'])
    c.rr(27,495,88,28,8,S)
    for t,x,on in (('Général',71,1),('Son',155,0),('Compte',235,0)): c.text(x,513,t,13,T['TEXTE1'] if on else T['TEXTE2'],'b' if on else 'r',anchor='c')
    mx,my=320,322
    c.shadow(mx,my,214,172,10,14,4,T['sh'][0]); c.rr(mx-1,my-1,216,174,11,T['LIGNE_FORTE'] if not T['dark'] else 0x3A3F49); c.rr(mx,my,214,172,10,T['MENU'])
    R=my+6
    for t,sc,st in (('Nouveau','Ctrl+N',0),('Ouvrir…','Ctrl+O',1),('Enregistrer','Ctrl+S',0),('-','',0),('Exporter en PDF','',2),('Quitter','Ctrl+Q',0)):
        if t=='-': c.line(mx+12,R+4,190,T['LIGNE']); R+=9; continue
        if st==1: c.rr(mx+6,R,202,30,6,T['ACCENT'])
        fg=0xFFFFFF if st==1 else (T['TEXTE3'] if st==2 else T['TEXTE1'])
        c.text(mx+16,R+20,t,13,fg); c.text(mx+198,R+20,sc,12,0xFFFFFF if st==1 else T['TEXTE3'],anchor='r'); R+=30
    dx,dy,dw,dh=566,178,214,150
    c.shadow(dx,dy,dw,dh,12,18,6,T['sh'][0]*1.2); c.rr(dx-1,dy-1,dw+2,dh+2,13,T['LIGNE_FORTE'] if not T['dark'] else 0x3A3F49); c.rr(dx,dy,dw,dh,12,T['FACE'])
    c.text(dx+18,dy+32,'Enregistrer ?',16,T['TEXTE1'],'b')
    c.text(dx+18,dy+56,'Le document a changé depuis',13,T['TEXTE2']); c.text(dx+18,dy+74,'le dernier enregistrement.',13,T['TEXTE2'])
    button(c,T,dx+18,dy+dh-46,78,32,'Annuler'); button(c,T,dx+dw-18-92,dy+dh-46,92,32,'Enregistrer',True)
    c.rr(566,350,74,26,6,T['TEXTE1']); c.text(603,367,'Fichiers',12,T['FACE'],anchor='c')
    tx=566; ty=416
    c.text(tx,ty,'Titre 20',20,T['TEXTE1'],'b'); c.text(tx,ty+26,'Section 16',16,T['TEXTE1'],'b'); c.text(tx,ty+48,'Texte 13 régulier',13,T['TEXTE1']); c.text(tx,ty+68,'Libellé 13 semi-gras',13,T['TEXTE1'],'b'); c.text(tx,ty+88,'Secondaire 12',12,T['TEXTE2'])
if __name__=="__main__":
    out=sys.argv[1]; os.makedirs(out,exist_ok=True)
    for n,nm in (('g','givre'),('n','noir')):
        T=theme(n)
        for fn,f in (('bureau',desk),('lanceur',launcher),('reglages',reglages),('fichiers',fichiers),('letters',letters)):
            c=C(800,600); f(c,T); c.out(f'{out}/a-{nm}-{fn}.png')
        B=themeB(n)
        for fn,f in (('bureau',b_desk),('reglages',b_reg),('fichiers',b_fich)):
            c=C(800,600); f(c,B); c.out(f'{out}/b-{nm}-{fn}.png')
    for n,nm in (('g','givre'),('n','noir')):
        c=C(800,560); board(c,theme(n)); c.out(f'{out}/a-{nm}-composants.png')
    T=theme('g'); c=C(1024,768); desk(c,T); c.out(f'{out}/a-givre-bureau-1024.png')
    T=theme('n'); c=C(1024,768); fichiers(c,T); c.out(f'{out}/a-noir-fichiers-1024.png')
