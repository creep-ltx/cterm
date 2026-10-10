#include "src/vt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *frags[]={"\x1b[","\x1b[?","\x1b]","\x07","\x1b\\",";","999999","0","1","2","5","9","24","80","H","J","K","L","M","@","P","X","S","T","r","b","I","Z","h","l","m","g","\x1b""7","\x1b""8","\x1b""D","\x1b""M","\x1b""E","\x1b(0","\x1b(B","\n","\n\n\n\n","\r","\t","\b","x","hello world ","\xe4\xb8\xad","\xf0\x9f\x98\x80","\xc3","\x80","\xff","\x9b","1049","47","6","7","4","\x1b#8","38;5;","48;2;1;2;3","52;c;aGVsbG8=","0;title","\x0c","\x0b","\x1b""c","!p","$p"," q","\x1b[3J","\x1b[1;1H\x1b[M","\x1b[M","\x1b[999L","\x1b[999M"};
int main(int argc,char**argv){
 unsigned seed=argc>1?atoi(argv[1]):1; int it;
 srand(seed);
 for(it=0;it<2000;it++){
  int cols=2+rand()%30, rows=2+rand()%12, k, mode=rand()%4;
  Vt*v=vt_new(cols,rows,rand()%3?20:0);
  if(mode==1)vt_set_cp437(v,1); if(mode==2)vt_set_amiga(v,1); if(mode==3)vt_set_utf8(v,0);
  for(k=0;k<400;k++){
   int r=rand()%100; VtChanges c; char rb[64];
   if(r<2) vt_resize(v,1+rand()%40,1+rand()%15);
   else if(r<90){const char*f=frags[rand()%(sizeof frags/sizeof*frags)];vt_write(v,(const uint8_t*)f,strlen(f));}
   else {unsigned char b[16];int n=rand()%16,j;for(j=0;j<n;j++)b[j]=rand();vt_write(v,b,n);}
   if(rand()%5==0){vt_take_changes(v,&c);vt_take_reply(v,rb,sizeof rb);}
   {int w,y,b=rand()%25; for(y=0;y<vt_rows(v);y++){const VtCell*l=vt_line(v,y,b,&w); if(l&&w>0){volatile uint32_t q=l[w-1].cp;(void)q;}}}
  }
  {int l;free(vt_take_clip(v,&l));}
  vt_free(v);
 }
 return 0;
}
