#include "src/vt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *frags[]={"\x1b[","\x1b[?","\x1b]","\x07","\x1b\\",";","999999","0","1","2","5","9","24","80","H","J","K","L","M","@","P","X","S","T","r","b","I","Z","h","l","m","g","\x1b""7","\x1b""8","\x1b""D","\x1b""M","\x1b""E","\x1b(0","\x1b(B","\n","\n\n\n\n","\r","\t","\b","x","hello world ","\xe4\xb8\xad","\xf0\x9f\x98\x80","\xc3","\x80","\xff","\x9b","\xd0\x9b","\xc5\x9b","\xc4","\x9b"">?","\x9b""0 p"," ","1049","47","6","7","4","\x1b#8","38;5;","48;2;1;2;3","52;c;aGVsbG8=","0;title","\x0c","\x0b","\x1b""c","!p","$p"," q","\x1b[3J","\x1b[1;1H\x1b[M","\x1b[M","\x1b[999L","\x1b[999M",
 /* pictures: sixel, iTerm2 files, kitty's APC, their queries */
 "\x1bP0;1;0q","\x1bPq","\x90q","\"1;1;20;12","\"1;1;9999;9999","#0;2;100;0;0","#1;1;120;50;100","#7","#300","!5","!99999","~","?","@","N","-","$","-\x1b\\","\x1b\\",
 "\x1b]1337;File=inline=1;width=2:","\x1b]1337;File=inline=1;height=50%;width=3px:","\x1b]1337;File=name=x:","UDYKMiAyCjI1NQr/AAAA/wAAAP///wAA","UDYKMSAxCjI1NQr/AAA=","AAAA","=","\x1b_Gf=100,a=T;","iVBORw0KGgo=",
 "\x1b[?80h","\x1b[?80l","\x1b[?8452h","\x1b[c","\x1b[14t","\x1b[16t","\x1b[?1;1;0S","\x1b[?2;1;0S","\x18"};
/* iTerm2 files: P6 PPMs, as tests/pic_test.c decodes them */
static int ppm(const uint8_t *f,long len,uint32_t **o,int *w,int *h){int i,off=0,mx;
 char t[64]; if(len<11||f[0]!='P'||f[1]!='6')return 0; memcpy(t,f+2,len-2<63?len-2:63); t[len-2<63?len-2:63]=0;
 if(sscanf(t,"%d %d %d%n",w,h,&mx,&off)!=3)return 0; off+=3;
 if(*w<=0||*h<=0||*w>64||*h>64||len<off+(long)*w**h*3)return 0;
 *o=malloc((size_t)*w**h*4); for(i=0;i<*w**h;i++)(*o)[i]=f[off+i*3]<<16|f[off+i*3+1]<<8|f[off+i*3+2]; return 1;}
static void ppmfree(uint32_t*p){free(p);}
/* every picture cell shows a picture there is, inside it; its row is
 * marked */
static void pics_ok(Vt*v,int b){int y,x,w;for(y=0;y<vt_rows(v);y++){const VtCell*l=vt_line(v,y,b,&w);
 for(x=0;l&&x<w;x++)if(VT_IS_PIC(l[x].cp)){const VtPic*p=vt_pic(v,l[x].cp);
  if(!p||!VT_ROW_PICS(l)||(int)VT_PIC_TX(l[x].cp)*p->cw>=p->w||(int)VT_PIC_TY(l[x].cp)*p->ch>=p->h){fprintf(stderr,"bad picture cell\n");abort();}}}}
int main(int argc,char**argv){
 unsigned seed=argc>1?atoi(argv[1]):1; int it;
 srand(seed);
 for(it=0;it<2000;it++){
  int cols=2+rand()%30, rows=2+rand()%12, k, mode=rand()%5;
  Vt*v=vt_new(cols,rows,rand()%3?20:0);
  vt_decode=ppm; vt_decode_free=ppmfree;
  vt_set_pictures(v,rand()%4!=0,rand()%2?4000:200000); vt_set_cell_px(v,1+rand()%9,1+rand()%17,0);
  if(mode==1)vt_set_cp437(v,1); if(mode==2)vt_set_amiga(v,1); if(mode==3)vt_set_utf8(v,0);
  if(mode==4){vt_set_amiga(v,1);vt_set_utf8(v,1);}   /* the Amiga Shell in UTF-8 */
  for(k=0;k<400;k++){
   int r=rand()%100; VtChanges c; char rb[64];
   if(r<2) vt_resize(v,1+rand()%40,1+rand()%15);
   else if(r<90){const char*f=frags[rand()%(sizeof frags/sizeof*frags)];vt_write(v,(const uint8_t*)f,strlen(f));}
   else {unsigned char b[16];int n=rand()%16,j;for(j=0;j<n;j++)b[j]=rand();vt_write(v,b,n);}
   if(rand()%5==0){vt_take_changes(v,&c);vt_take_reply(v,rb,sizeof rb);}
   if(rand()%7==0)pics_ok(v,rand()%25);
   {int w,y,b=rand()%25; for(y=0;y<vt_rows(v);y++){const VtCell*l=vt_line(v,y,b,&w); if(l&&w>0){volatile uint32_t q=l[w-1].cp;(void)q;}}}
  }
  pics_ok(v,0);
  {int l;free(vt_take_clip(v,&l));}
  vt_free(v);
 }
 return 0;
}
