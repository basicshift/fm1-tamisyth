// Tamisynth sprites: shape-built 40x40 pixel creatures, outlined and shaded, written to firmware/src/sprites.h
// as an RGB565 palette (index 0 = transparent) and one byte a pixel.   node tools/sprites.mjs [preview.html]
import { writeFileSync } from 'node:fs';
const N = 40;
function h2l(h){h=h.replace('#','');let r=parseInt(h.slice(0,2),16)/255,g=parseInt(h.slice(2,4),16)/255,b=parseInt(h.slice(4,6),16)/255;let mx=Math.max(r,g,b),mn=Math.min(r,g,b),l=(mx+mn)/2,s=0,H=0;if(mx!==mn){let d=mx-mn;s=l>.5?d/(2-mx-mn):d/(mx+mn);H=mx===r?(g-b)/d+(g<b?6:0):mx===g?(b-r)/d+2:(r-g)/d+4;H*=60}return[H,s,l]}
function l2h(H,s,l){H=((H%360)+360)%360;s=Math.max(0,Math.min(1,s));l=Math.max(0,Math.min(1,l));const k=n=>(n+H/30)%12,a=s*Math.min(l,1-l),f=n=>l-a*Math.max(-1,Math.min(k(n)-3,9-k(n),1));return'#'+[f(0),f(8),f(4)].map(v=>Math.round(v*255).toString(16).padStart(2,'0')).join('')}
const tw=(H,t,a)=>{let d=((t-H+540)%360)-180;return H+d*a};
function ramp(hex){const[H,s,l]=h2l(hex);return{d:l2h(tw(H,255,.18),s*1.05,l*.68),m:hex,l:l2h(tw(H,55,.12),s,l+(1-l)*.3),hi:l2h(tw(H,55,.2),s*.9,l+(1-l)*.62),o:l2h(tw(H,265,.35),s*.75,l*.28)}}
const E=(cx,cy,rx,ry,r)=>({t:'e',cx,cy,rx,ry,r}),R=(x,y,w,h,r)=>({t:'r',x,y,w,h,r}),T=(p,r)=>({t:'tri',p,r}),Ring=(cx,cy,rad,w,r)=>({t:'ring',cx,cy,rad,w,r}),B=(cx,cy,rx,ry,y0,y1,dx,r)=>({t:'band',cx,cy,rx,ry,y0,y1,dx,r}),P=(x,y,r)=>({t:'px',x,y,r});
function hit(s,x,y){switch(s.t){case'e':return((x-s.cx)/s.rx)**2+((y-s.cy)/s.ry)**2<=1;case'r':return x>=s.x&&x<s.x+s.w&&y>=s.y&&y<s.y+s.h;case'ring':{let d=Math.hypot(x-s.cx,y-s.cy);return Math.abs(d-s.rad)<=s.w}case'band':return y>=s.y0&&y<s.y1&&((x-s.dx-s.cx)/s.rx)**2+((y-s.cy)/s.ry)**2<=1;case'px':return Math.floor(x)===s.x&&Math.floor(y)===s.y;case'tri':{const[a,b,c]=s.p,sg=(p,q,r)=>(p[0]-r[0])*(q[1]-r[1])-(q[0]-r[0])*(p[1]-r[1]);const d1=sg([x,y],a,b),d2=sg([x,y],b,c),d3=sg([x,y],c,a);return!((d1<0||d2<0||d3<0)&&(d1>0||d2>0||d3>0))}}}
function render(cr){const reg=cr.regions,z={};cr.shapes.forEach((s,i)=>{if(!(s.r in z))z[s.r]=i});const rp={};for(const k in reg)rp[k]=ramp(reg[k].c);const L=[];for(let y=0;y<N;y++){L[y]=[];for(let x=0;x<N;x++){let v=null;for(const s of cr.shapes)if(hit(s,x+.5,y+.5))v=s.r;L[y][x]=v}}
const bb={};for(let y=0;y<N;y++)for(let x=0;x<N;x++){const v=L[y][x];if(!v)continue;const b=bb[v]||(bb[v]={x0:x,x1:x,y0:y,y1:y});b.x0=Math.min(b.x0,x);b.x1=Math.max(b.x1,x);b.y0=Math.min(b.y0,y);b.y1=Math.max(b.y1,y)}
const at=(x,y)=>x<0||y<0||x>=N||y>=N?null:L[y][x];const img=[];
for(let y=0;y<N;y++){img[y]=[];for(let x=0;x<N;x++){const v=L[y][x];let col=null;if(!v){for(const[a,b]of[[0,-1],[-1,0],[1,0],[0,1]]){const n=at(x+a,y+b);if(n&&reg[n].line!==false){col=reg[n].o||rp[n].o;break}}}else{const g=reg[v],r=rp[v];if(g.flat)col=g.c;else{let le=false;if(g.line!==false)for(const[a,b]of[[0,-1],[-1,0],[1,0],[0,1]]){const n=at(x+a,y+b);if(n&&n!==v&&z[n]<z[v]&&reg[n].flat!==true){le=true;break}}
if(le)col=g.o||r.o;else{const b=bb[v],w=b.x1-b.x0+1,h=b.y1-b.y0+1;let t='m';if(at(x+1,y)!==v||at(x,y+1)!==v||y>b.y0+h*.74||x>b.x0+w*.84)t='d';if(at(x,y-1)!==v||at(x-1,y)!==v)t=t==='d'?'m':'l';const hx=b.x0+w*.32,hy=b.y0+h*.26;if(Math.hypot((x-hx)/Math.max(1,w*.14),(y-hy)/Math.max(1,h*.12))<1)t='hi';else if(Math.hypot((x-hx)/Math.max(1,w*.26),(y-hy)/Math.max(1,h*.24))<1&&t==='m')t='l';col=r[t]}}}
img[y][x]=col}}return img}
const EYE={eyew:{c:'#ffffff',o:'#2a1f3a'},pupil:{c:'#221a30',flat:true,line:false},glint:{c:'#ffffff',flat:true,line:false},cheek:{c:'#ff9db0',flat:true,line:false},mouth:{c:'#3a2230',flat:true,line:false}};
const eyes=(pts)=>pts.flatMap(([x,y,rx,ry])=>[E(x,y,rx,ry,'eyew'),E(x+.5,y+.6,rx*.58,ry*.68,'pupil'),P(Math.floor(x-.4),Math.floor(y-.6),'glint')]);
const ecg=[[11,22],[12,22],[13,22],[14,22],[15,22],[16,21],[17,19],[18,17],[19,20],[20,24],[21,25],[22,23],[23,22],[24,22],[25,22],[26,22],[27,22],[28,22]];
export const C=[
{id:'EGG',n:'Pulse egg',regions:{egg:{c:'#efe6c8'},spot:{c:'#c9b98a',flat:true,line:false},crack:{c:'#e2564f',flat:true,line:false}},
 shapes:[E(20,22,9.5,12.5,'egg'),E(15,15,1.6,1.2,'spot'),E(24,29,1.8,1.3,'spot'),E(26,14,1.1,.9,'spot'),E(14,29,1,.8,'spot'),...ecg.map(([x,y])=>P(x,y,'crack'))]},
{id:'BLIP',n:'Blip',regions:Object.assign({body:{c:'#4fc3b5'},feet:{c:'#f29a4a'},belly:{c:'#f1e6c8'},cone:{c:'#4a5868'},cap:{c:'#c9d3dc',flat:true,line:false},stem:{c:'#3d8f86'},note:{c:'#f2c14e'},beak:{c:'#f29a4a'}},EYE),
 shapes:[E(15,35,3,1.6,'feet'),E(25,35,3,1.6,'feet'),R(19.4,7,1.3,7,'stem'),E(18.3,8,2.3,1.8,'note'),E(20,24,11,11,'body'),E(20,28.6,6.2,5.4,'belly'),E(20,28.6,3.2,2.8,'cone'),E(20,28.6,1.2,1,'cap'),...eyes([[15.5,20.5,2.3,2.7],[24.5,20.5,2.3,2.7]]),E(12.6,24.6,1.6,1,'cheek'),E(27.4,24.6,1.6,1,'cheek'),T([[18.6,23.4],[21.4,23.4],[20,25.6]],'beak')]},
{id:'KIK',n:'Kik',regions:Object.assign({body:{c:'#e9844a'},rim:{c:'#d9a441'},drum:{c:'#f3ead2'},lug:{c:'#8d93a0',flat:true,line:false},stick:{c:'#c98a52'}},EYE),
 shapes:[E(11,35.5,4,1.8,'body'),E(29,35.5,4,1.8,'body'),E(20,27,12,9.5,'body'),E(20,20,10,6,'body'),E(13,16,4.5,4,'body'),E(27,16,4.5,4,'body'),E(20,29.5,8,6.2,'rim'),E(20,29.5,6.3,4.6,'drum'),P(13,29,'lug'),P(26,29,'lug'),P(20,24,'lug'),P(20,35,'lug'),...eyes([[13,15.6,2.7,2.7],[27,15.6,2.7,2.7]]),R(16,22,8,1,'mouth'),E(11,21,1.6,1,'cheek'),E(29,21,1.6,1,'cheek'),T([[30,26],[31.2,25],[36,20]],'stick'),E(36.2,19.6,1.3,1.3,'stick')]},
{id:'HATLING',n:'Hatling',regions:Object.assign({body:{c:'#8b6cd9'},belly:{c:'#c8b8f0'},cym:{c:'#f2c14e'},cym2:{c:'#e0a93a'},rod:{c:'#c7ccd6'}},EYE),
 shapes:[E(8.5,31,6.5,2.3,'body'),E(3.5,29.3,2.2,1.7,'body'),R(12.5,31,3.2,5.5,'body'),R(23,31,3.2,5.5,'body'),E(19.5,29,10.5,6.2,'body'),E(29.5,21.5,7.2,5.6,'body'),E(21,32.6,7,2.1,'belly'),R(17.4,17.5,1.3,8,'rod'),E(18,25.6,9,1.7,'cym2'),E(18,22.6,9,1.7,'cym'),E(18,21.6,2.2,1.1,'cym'),...eyes([[31.5,19.6,2.1,2.4]]),R(30,23.6,5.5,.9,'mouth'),E(33.6,22,1.3,.8,'cheek')]},
{id:'CRATE',n:'Crate',regions:Object.assign({body:{c:'#8a6a4f'},band:{c:'#4a505e'},cup:{c:'#3a3f4a'},cupring:{c:'#e05a4f'},panel:{c:'#2f3540'},padR:{c:'#ff6b6b',line:false},padO:{c:'#ffb347',line:false},snout:{c:'#d9b892'},nose:{c:'#3a2620',flat:true,line:false}},EYE),
 shapes:[R(11.5,31,6,6,'body'),R(22.5,31,6,6,'body'),E(8.6,25,3.2,6.2,'body'),E(31.4,25,3.2,6.2,'body'),E(20,25.5,11,9,'body'),R(10,21,20,11,'body'),E(20,9.5,9.5,7,'band'),E(13,6,2.6,2.6,'body'),E(27,6,2.6,2.6,'body'),E(20,12.5,8.2,7,'body'),E(11.6,12.5,2.4,3.4,'cup'),E(28.4,12.5,2.4,3.4,'cup'),E(11.6,12.5,1,1.6,'cupring'),E(28.4,12.5,1,1.6,'cupring'),R(13,21,14,10,'panel'),R(14.5,22.5,3,3,'padR'),R(18.5,22.5,3,3,'padO'),R(22.5,22.5,3,3,'padR'),R(14.5,26.5,3,3,'padO'),R(18.5,26.5,3,3,'padR'),R(22.5,26.5,3,3,'padO'),E(20,15.6,3.4,2.2,'snout'),E(20,14.8,1.2,.8,'nose'),...eyes([[16.6,11.6,1.8,2.1],[23.4,11.6,1.8,2.1]])]},
{id:'FOURFLOOR',n:'Fourfloor',regions:Object.assign({body:{c:'#ff7bd5'},visor:{c:'#7ff0ff'},tile:{c:'#e6fbff',flat:true,line:false},tile2:{c:'#9fdcec',flat:true,line:false}},EYE),
 shapes:[R(12,30,2.4,6,'body'),R(16.4,30,2.4,6,'body'),R(21.2,30,2.4,6,'body'),R(25.6,30,2.4,6,'body'),E(13.2,36.4,2,1.2,'body'),E(17.6,36.4,2,1.2,'body'),E(22.4,36.4,2,1.2,'body'),E(26.8,36.4,2,1.2,'body'),E(20,24,9,8,'body'),E(20,12,7.5,6.5,'body'),
  ...[0,1,2,3].flatMap(i=>[0,1,2].map(j=>P(15+i*3+(j%2),21+j*3,(i+j)%2?'tile':'tile2'))),R(13,10,14,3.2,'visor'),...eyes([[16.5,15,1.7,1.9],[23.5,15,1.7,1.9]]),R(18,18.2,4,.9,'mouth')]},
{id:'CHOPPER',n:'Chopper',regions:Object.assign({body:{c:'#ff9f43'},body2:{c:'#ff9f43'},body3:{c:'#ff9f43'},spike:{c:'#ffe08a'},belly:{c:'#ffd9a8'}},EYE),
 shapes:[T([[13,10],[15.5,4.5],[17.5,10.5]],'spike'),T([[18.5,9],[21,3],[23,9.5]],'spike'),T([[24,10],[26.5,5.5],[27.5,11]],'spike'),B(20,22,10,11.5,9.5,16.5,-2,'body'),B(20,22,10,11.5,17.5,25,2,'body2'),E(22,22,5,2.6,'belly'),B(20,22,10,11.5,26,33.5,-1,'body3'),R(13,33,3,3.5,'body3'),R(21,33,3,3.5,'body3'),...eyes([[14.6,13,2,2.3],[21.6,13,2,2.3]]),R(15,15.8,5,.9,'mouth')]},
{id:'ECHO',n:'Echo',regions:Object.assign({body:{c:'#9fd0ff'},feet:{c:'#5d86b8'},lid:{c:'#2a3a55',flat:true,line:false},echo1:{c:'#7fb4ea'},echo2:{c:'#b9d9fb'}},EYE),
 shapes:[E(15.5,34,2.2,1.5,'feet'),E(23.5,34,2.2,1.5,'feet'),E(13.5,24,6,6,'body'),E(19.5,20.5,7,7,'body'),E(25,23.5,6,6,'body'),E(19.5,27,8,6.5,'body'),E(11.5,28,4.5,4.5,'body'),E(27,28,4.5,4.5,'body'),R(14.5,23,3,1,'lid'),R(21.5,23,3,1,'lid'),E(12.6,26,1.6,1,'cheek'),E(26.4,26,1.6,1,'cheek'),R(18.5,27,3,.9,'mouth'),Ring(33,17,2.6,.7,'echo1'),Ring(36,11,1.8,.6,'echo2')]}
];
const to565=h=>{const r=parseInt(h.slice(1,3),16),g=parseInt(h.slice(3,5),16),b=parseInt(h.slice(5,7),16);return((r>>3)<<11)|((g>>2)<<5)|(b>>3)};
let out=`/* Generated by tools/sprites.mjs: do not edit. 40 x 40, index 0 = transparent, palette RGB565. */\n#define SPR_N ${N}u\ntypedef struct { const uint16_t *pal; const uint8_t *px; } sprite_t;\n`;
const prev=[];
for(const cr of C){const img=render(cr);const pal=['-'];const px=[];for(let y=0;y<N;y++)for(let x=0;x<N;x++){const c=img[y][x];if(!c){px.push(0);continue}let i=pal.indexOf(c);if(i<0){pal.push(c);i=pal.length-1}px.push(i)}
 if(pal.length>256)throw new Error(cr.id+': too many colours');
 out+=`static const uint16_t SPR_${cr.id}_PAL[${pal.length}] = {0,${pal.slice(1).map(c=>'0x'+to565(c).toString(16).padStart(4,'0')+'u').join(',')}};\n`;
 out+=`static const uint8_t SPR_${cr.id}_PX[${N*N}] = {\n`;for(let y=0;y<N;y++)out+='  '+px.slice(y*N,y*N+N).join(',')+',\n';out+='};\n';
 prev.push({n:cr.n,img});}
out+=`enum { ${C.map(c=>'SP_'+c.id).join(', ')}, SP_COUNT };\nstatic const sprite_t SPRITES[SP_COUNT] = {${C.map(c=>`{SPR_${c.id}_PAL, SPR_${c.id}_PX}`).join(', ')}};\n`;
writeFileSync(new URL('../firmware/src/sprites.h', import.meta.url), out);
console.log('sprites.h:', C.length, 'sprites');
