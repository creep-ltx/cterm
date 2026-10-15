#include "src/vt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *frags[]={"\x1b[","\x1b[?","\x1b]","\x07","\x1b\\",";","999999","0","1","2","5","9","24","80","H","J","K","L","M","@","P","X","S","T","r","b","I","Z","h","l","m","g","\x1b""7","\x1b""8","\x1b""D","\x1b""M","\x1b""E","\x1b(0","\x1b(B","\n","\n\n\n\n","\r","\t","\b","x","hello world ","\xe4\xb8\xad","\xf0\x9f\x98\x80","\xc3","\x80","\xff","\x9b","\xd0\x9b","\xc5\x9b","\xc4","\x9b"">?","\x9b""0 p"," ","1049","47","6","7","4","\x1b#8","38;5;","48;2;1;2;3","52;c;aGVsbG8=","0;title","\x0c","\x0b","\x1b""c","!p","$p"," q","\x1b[3J","\x1b[1;1H\x1b[M","\x1b[M","\x1b[999L","\x1b[999M",
 /* pictures: sixel, iTerm2 files, kitty's APC, their queries */
 "\x1bP0;1;0q","\x1bPq","\x90q","\"1;1;20;12","\"1;1;9999;9999","#0;2;100;0;0","#1;1;120;50;100","#7","#300","!5","!99999","~","?","@","N","-","$","-\x1b\\","\x1b\\",
 "\x1b]1337;File=inline=1;width=2:","\x1b]1337;File=inline=1;height=50%;width=3px:","\x1b]1337;File=name=x:","UDYKMiAyCjI1NQr/AAAA/wAAAP///wAA","UDYKMSAxCjI1NQr/AAA=","AAAA","=","\x1b_Gf=100,a=T;","iVBORw0KGgo=",
 "\x1b_Ga=T,f=32,s=2,v=2,o=z,i=3;","eJz7z8Dw/z8SBgBDzAf5","\x1b_Ga=T,f=24,s=3,v=2,m=1;","AP8AAP8AAP8A","\x1b_Gm=1;","\x1b_Gm=0;","AP8AAP8AAP8A\x1b\\","\x1b_Ga=t,f=24,s=1,v=1,i=7;AP8A\x1b\\","\x1b_Ga=p,i=7,c=3,r=2\x1b\\","\x1b_Ga=p,i=3,C=1,x=1,w=1\x1b\\","\x1b_Ga=p,I=2\x1b\\","\x1b_Ga=d\x1b\\","\x1b_Ga=d,d=I,i=7\x1b\\","\x1b_Ga=d,d=c\x1b\\","\x1b_Ga=d,d=y,y=1\x1b\\","\x1b_Ga=q,i=1,s=1,v=1,f=24;AAAA\x1b\\","\x1b_Ga=t,I=2,f=100;UDYKMiAyCjI1NQr/AAAA/wAAAP///wAA\x1b\\","\x1b_Gq=2,a=T,f=24,s=99999,v=99999;","\x9fGa=d\x9c",
 "\x1bP2q","\x1bP7q","\"2;1","\"1;3;8;4","\"5;1;30;6","\x1b]1337;MultipartFile=inline=1;width=2\x07","\x1b]1337;FilePart=UDYKMiAyCjI1NQr/AAAA","\x1b]1337;FilePart=/wAAAP///wAA\x07","\x1b]1337;FileEnd\x07",
 "\x1b[?80h","\x1b[?80l","\x1b[?8452h","\x1b[c","\x1b[14t","\x1b[16t","\x1b[?1;1;0S","\x1b[?2;1;0S","\x18"};
/* iTerm2 files: P6 PPMs, as tests/pic_test.c decodes them */
static int ppm(const uint8_t *f,long len,uint32_t **o,int *w,int *h){int i,off=0,mx;
 char t[64]; if(len<11||f[0]!='P'||f[1]!='6')return 0; memcpy(t,f+2,len-2<63?len-2:63); t[len-2<63?len-2:63]=0;
 if(sscanf(t,"%d %d %d%n",w,h,&mx,&off)!=3)return 0; off+=3;
 if(*w<=0||*h<=0||*w>64||*h>64||len<off+(long)*w**h*3)return 0;
 *o=malloc((size_t)*w**h*4); for(i=0;i<*w**h;i++)(*o)[i]=f[off+i*3]<<16|f[off+i*3+1]<<8|f[off+i*3+2]; return 1;}
static void ppmfree(uint32_t*p){free(p);}
long zinflate(const uint8_t *in, long n, uint8_t *out, long max);
/* every picture cell shows a picture there is, inside it; its row is
 * marked; inside its box of cells (a new letter size cuts it anew) */
static void pics_ok(Vt*v,int b){int y,x,w;for(y=0;y<vt_rows(v);y++){const VtCell*l=vt_line(v,y,b,&w);
 for(x=0;l&&x<w;x++)if(VT_IS_PIC(l[x].cp)){const VtPic*p=vt_pic(v,l[x].cp);
  if(!p||!VT_ROW_PICS(l)||(int)VT_PIC_TX(l[x].cp)>=(p->ow+p->ocw-1)/p->ocw||(int)VT_PIC_TY(l[x].cp)>=(p->oh+p->och-1)/p->och||
     (p->orgb&&(p->w>p->ow*p->cw/p->ocw+1||p->h>p->oh*p->cw/p->ocw+1))){fprintf(stderr,"bad picture cell\n");abort();}}}}
int main(int argc,char**argv){
 unsigned seed=argc>1?atoi(argv[1]):1; int it;
 srand(seed);
 /* inflate.c on broken zlib data: a dynamic block (347 bytes, 700 out), a stored one; bits flipped, cut */
 {static const uint8_t zd[]={120,218,29,146,137,21,195,48,8,67,87,209,106,136,115,255,9,242,157,215,212,105,109,16,58,92,82,142,78,86,92,171,234,164,219,172,212,134,83,209,209,167,221,213,229,56,242,170,183,87,171,17,75,77,244,235,150,92,234,182,239,60,222,140,222,117,72,241,234,0,5,197,230,111,109,171,51,102,38,119,129,165,79,234,125,152,148,205,43,231,43,42,110,104,188,80,196,52,180,52,201,135,173,126,184,9,100,23,24,128,206,212,165,167,116,93,28,213,112,58,108,43,31,230,205,207,54,16,20,247,51,230,197,164,178,218,21,212,47,35,14,161,93,240,221,144,7,244,139,41,74,156,5,91,120,70,23,28,120,223,68,100,56,124,1,243,131,147,7,111,122,160,179,207,174,245,207,254,135,218,39,3,157,81,152,35,111,237,147,206,224,179,214,163,223,242,172,55,84,207,180,121,184,144,126,206,233,141,87,60,29,174,36,148,237,209,242,24,13,155,152,68,32,155,137,227,100,151,217,4,134,44,204,228,4,182,164,182,108,77,13,101,167,151,42,50,17,128,46,188,67,215,203,150,153,207,68,187,251,145,70,220,60,194,132,201,21,208,184,189,198,175,231,242,17,239,31,244,43,35,244,204,57,236,39,124,228,231,93,167,209,143,110,186,122,227,217,214,69,31,195,77,37,166,250,55,148,187,68,89,110,177,8,127,167,227,22,97,100,245,7,94,97,2,21,60,249,41,156,130,41,43,204,84,111,248,187,60,147,108,144,185,63,136,228,232,115,
   120,1,1,7,0,248,255,115,116,111,114,101,100,33,11,239,2,179};
  uint8_t b[400],o[800]; int i,j,n; if(zinflate(zd,347,o,700)!=700){fprintf(stderr,"inflate broken\n");abort();}
  for(i=0;i<4000;i++){const uint8_t*src=i%2?zd:zd+347; n=i%2?347:(int)sizeof zd-347; memcpy(b,src,n);
   for(j=rand()%4;j>=0;j--)b[rand()%n]^=1<<(rand()%8);
   zinflate(b,rand()%3?n:rand()%(n+1),o,rand()%2?700:rand()%800);}}
 for(it=0;it<2000;it++){
  int cols=2+rand()%30, rows=2+rand()%12, k, mode=rand()%5;
  Vt*v=vt_new(cols,rows,rand()%3?20:0);
  vt_decode=ppm; vt_decode_free=ppmfree; vt_inflate=zinflate;
  vt_set_pictures(v,rand()%4!=0,rand()%2?4000:200000); vt_set_cell_px(v,1+rand()%9,1+rand()%17,0);
  if(mode==1)vt_set_cp437(v,1); if(mode==2){vt_set_utf8(v,0);vt_set_amiga(v,1);} if(mode==3)vt_set_utf8(v,0);
  if(mode==4){vt_set_amiga(v,1);vt_set_utf8(v,1);}   /* the Amiga Shell in UTF-8 */
  for(k=0;k<400;k++){
   int r=rand()%100; VtChanges c; char rb[64];
   if(r<2) vt_resize(v,1+rand()%40,1+rand()%15);
   else if(r<3) vt_set_cell_px(v,1+rand()%17,1+rand()%33,0);   /* a new letter size */
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
