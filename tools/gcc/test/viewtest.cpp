#include <stdio.h>
#include <string.h>
#include <uikit.h>
static struct ui_vars ui; static long v, tb, lbl; static int mx=-1,my=-1,clicks,keys,wh; static char last[64]="-";
static void paint(long,long w,long h){ const unsigned*k=(const unsigned*)ui_jetons(); ui_fill(0,0,w,h,k[1]&0xFFFFFF); ui_frame(4,4,w-8,h-8,k[5]&0xFFFFFF);
 char b[100]; snprintf(b,100,"mouse %d,%d clicks %d keys %d wheel %d last %s",mx,my,clicks,keys,wh,last); ui_text((long)b,16,40,k[7]&0xFFFFFF,14,(long)ui.font,0); if(mx>=0) ui_vp_disc(mx*16,my*16,6*16,k[10]&0xFFFFFF);}
static long mouse(long me,long kind,long x,long y,long info){ mx=x;my=y; if(kind==1){clicks=info&255;snprintf(last,64,"down c%d b%x m%x",(int)(info&255),(int)((info>>8)&255),(int)((info>>16)&255));} ui_view_dirty(me); return 0;}
static long key(long me,long hid,long ch,long m){ keys++; snprintf(last,64,"key %x ch %x m %x",(int)hid,(int)ch,(int)m); ui_view_dirty(me); return 0;}
static long wheel(long me,long n){ wh+=n; ui_view_dirty(me); return 0;}
static void rs(long w,long h){ ui_w_move(v,8,44,w-16,h-52); ui_view_dirty(v); ui_w_draw(0); ui_update_all(); }
int main(){ if(!uikit_open(&ui)) return 1; long s[4]={1024,768,0,0}; ui_screen((long)s); ui_window_ex(0,s[3],640,420,(long)"ViewTest",WF_RESIZABLE); ui_on_resize((long)rs,0);
 const unsigned*k=(const unsigned*)ui_jetons(); ui_fill(0,0,ui.w,ui.h,k[0]&0xFFFFFF);
 tb=ui_toolbar(8,4,400,0); ui_toolbar_add(tb,(long)"+",(long)"Plus",0); v=ui_view(8,44,ui.w-16,ui.h-52,UVW_FOCUS); ui_view_on(v,(long)paint,(long)mouse,(long)key,(long)wheel);
 ui_menu((long)"MFichier\nI1\tQuitter\tCtrl+Q\n"); ui_w_focus(v); ui_w_draw(0); ui_update_all();
 for(;;){ long t=ui_event(16); if(t==WEV_CLOSE)break; if(t>0){ long c,tk; uikit_w_event(&c,&tk);} else ui_w_idle(); ui_view_flush(); } return 0;}
