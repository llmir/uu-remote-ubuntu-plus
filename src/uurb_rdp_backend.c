#define WIN32_LEAN_AND_MEAN
#include "uurb_rdp_backend.h"
#include "uurb_ready.h"
#include "full-input.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static HANDLE channel=INVALID_HANDLE_VALUE;
static uint8_t cap[UI_HEADER];
static uint64_t sequence=1,server_start,blocked_start;
static DWORD server_pid,blocked_pid;
static BOOL poisoned;
static uint32_t control_status=UI_AMBIGUOUS;
static uint8_t pressed[1024];
static uurb_rdp_state source_state;
static uint64_t clock_ns(void);
enum reject_reason {
 REJECT_DECODE_OTHER=1,REJECT_MOUSE_FLAGS,REJECT_ABSOLUTE_RANGE,
 REJECT_XBUTTON_DATA,REJECT_WHEEL_DATA,REJECT_EMPTY,
 REJECT_MISSING_CAPS,REJECT_PREFLIGHT_ALLOC,REJECT_PREFLIGHT_STATUS
};
static volatile LONG reject_diagnostic_count;
static void rejection_diagnostic(enum reject_reason reason,DWORD count,
 const INPUT* inputs,DWORD index,uint32_t required,BOOL decoded,
 BOOL preflight,enum ui_status status,uint64_t deadline){
 DWORD saved_error=GetLastError();LONG slot;
 do{
  slot=InterlockedCompareExchange(&reject_diagnostic_count,0,0);
  if(slot>=32){return;}
 }while(InterlockedCompareExchange(&reject_diagnostic_count,slot+1,slot)!=slot);
 const INPUT* in=inputs&&index<count?inputs+index:NULL;
 BOOL mouse=in&&in->type==INPUT_MOUSE;
 BOOL absolute=mouse&&(in->mi.dwFlags&MOUSEEVENTF_MOVE)&&(in->mi.dwFlags&MOUSEEVENTF_ABSOLUTE);
 int vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN);
 int vw=GetSystemMetrics(SM_CXVIRTUALSCREEN),vh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
 int pw=GetSystemMetrics(SM_CXSCREEN),ph=GetSystemMetrics(SM_CYSCREEN);
 uint64_t now=clock_ns();
 fprintf(stderr,"UURB public reject tick-ms=%llu slot=%lu reason=%u broker-win-pid=%lu count=%lu index=%lu mouse=%u absolute=%u range-applicable=%u x-range-ok=%u y-range-ok=%u time-nonzero=%u extra-nonzero=%u data-nonzero=%u decoded=%u required=0x%08lx caps=0x%08lx missing=0x%08lx preflight=%u status=%u seq=%llu generation=%llu epoch=%llu expiry-ms=%llu monitors=%d primary=%dx%d virtual=%dx%d virtual-origin-zero=%u primary-virtual-equal=%u viewport-virtual-equal=%u\n",
  (unsigned long long)GetTickCount64(),(unsigned long)slot+1,(unsigned)reason,
  (unsigned long)GetCurrentProcessId(),(unsigned long)count,(unsigned long)index,
  (unsigned)mouse,(unsigned)absolute,(unsigned)absolute,
  (unsigned)(absolute&&in->mi.dx>=0&&in->mi.dx<=65535),
  (unsigned)(absolute&&in->mi.dy>=0&&in->mi.dy<=65535),
  (unsigned)(mouse&&in->mi.time!=0),(unsigned)(mouse&&in->mi.dwExtraInfo!=0),
  (unsigned)(mouse&&in->mi.mouseData!=0),(unsigned)decoded,
  (unsigned long)required,(unsigned long)ui_get32(cap+128),
  (unsigned long)(required&~ui_get32(cap+128)),(unsigned)preflight,(unsigned)status,
  (unsigned long long)sequence,(unsigned long long)ui_get64(cap+16),
  (unsigned long long)ui_get64(cap+72),
  (unsigned long long)(now<deadline?(deadline-now)/UINT64_C(1000000):0),
  GetSystemMetrics(SM_CMONITORS),pw,ph,vw,vh,(unsigned)(vx==0&&vy==0),
  (unsigned)(pw>0&&ph>0&&pw==vw&&ph==vh&&vx==0&&vy==0),
  (unsigned)((int32_t)ui_get32(cap+108)==vx&&(int32_t)ui_get32(cap+112)==vy&&
   vw>0&&vh>0&&ui_get32(cap+116)==(uint32_t)vw&&ui_get32(cap+120)==(uint32_t)vh));
 SetLastError(saved_error);
}
static uint64_t clock_ns(void){
 LARGE_INTEGER c,f;if(!QueryPerformanceCounter(&c)||!QueryPerformanceFrequency(&f)||f.QuadPart<=0){return UINT64_MAX;}
 uint64_t n=(uint64_t)c.QuadPart,r=(uint64_t)f.QuadPart;
 return (n/r)*UINT64_C(1000000000)+(n%r)*UINT64_C(1000000000)/r;
}
BOOL uurb_rdp_enabled(void){char mode[32];DWORD n=GetEnvironmentVariableA("UURB_INPUT_ROUTE",mode,sizeof(mode));return n<sizeof(mode)&&n>0&&!strcmp(mode,"rdp-public");}
BOOL uurb_rdp_publish_identity(void){return !uurb_rdp_enabled()||uurb_ready_publish(L"UURB_FULL_BROKER_READY","broker");}
static void close_channel(BOOL uncertain){
 if(uncertain){poisoned=TRUE;blocked_pid=server_pid;blocked_start=server_start;}
 if(channel!=INVALID_HANDLE_VALUE){CloseHandle(channel);channel=INVALID_HANDLE_VALUE;}
 SecureZeroMemory(cap,sizeof(cap));SecureZeroMemory(&source_state,sizeof(source_state));sequence=1;
}
BOOL uurb_rdp_needs_recovery(void){return poisoned;}
void uurb_rdp_upstream_closed(void){
 close_channel(TRUE); /* Actual EOF makes plugin drain while its context is live. */
 fprintf(stderr,"UURB rdp-public upstream-ended supervisor-replace-owned-sdl=1\n");
}
static BOOL peer(void){
 ULONG pid=0;if(channel==INVALID_HANDLE_VALUE||!GetNamedPipeServerProcessId(channel,&pid)||pid!=server_pid){return FALSE;}
 HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!p){return FALSE;}
 uint64_t stamp=uurb_creation(p);CloseHandle(p);return stamp&&stamp==server_start;
}
static BOOL transfer(uint8_t* bytes,DWORD count,BOOL writing,uint64_t deadline){
 DWORD used=0;while(used<count){
  if(clock_ns()>=deadline||!peer()){return FALSE;}DWORD got=0;
  if(!writing){DWORD available=0;if(!PeekNamedPipe(channel,NULL,0,NULL,&available,NULL)){return FALSE;}if(!available){Sleep(1);continue;}}
  BOOL ok=writing?WriteFile(channel,bytes+used,count-used,&got,NULL):ReadFile(channel,bytes+used,count-used,&got,NULL);
  if(!ok){if(GetLastError()!=ERROR_NO_DATA){return FALSE;}Sleep(1);continue;}
  if(!got){Sleep(1);continue;}used+=got;
 }
 return clock_ns()<deadline;
}
static BOOL valid_cap(const uint8_t* bytes){
 return ui_get32(bytes)==UI_MAGIC&&ui_get32(bytes+4)==UI_VERSION&&ui_get32(bytes+8)==UI_HEADER&&
 ui_get32(bytes+12)==UI_OK&&ui_get64(bytes+16)&&ui_get64(bytes+72)&&
 ui_get32(bytes+80)>0&&ui_get32(bytes+80)<=65536&&ui_get32(bytes+84)>0&&ui_get32(bytes+84)<=65536&&
 ui_get32(bytes+116)>1&&ui_get32(bytes+120)>1&&!ui_get32(bytes+136);
}
static BOOL connect_channel(uint64_t deadline){
 DWORD pid;uint64_t started;if(!uurb_ready_read(L"UURB_FULL_SDL_READY","sdl",&pid,&started)){return FALSE;}
 if(poisoned&&pid==blocked_pid&&started==blocked_start){SetLastError(ERROR_NOT_READY);return FALSE;}
 if(channel!=INVALID_HANDLE_VALUE){if(pid==server_pid&&started==server_start&&peer()){return TRUE;}close_channel(FALSE);}
 server_pid=pid;server_start=started;
 char name[160];DWORD n=GetEnvironmentVariableA("UURB_FULL_PIPE",name,sizeof(name));
 if(!n||n>=sizeof(name)||strncmp(name,"\\\\.\\pipe\\uurb-full-",19)){return FALSE;}
 channel=CreateFileA(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 if(channel==INVALID_HANDLE_VALUE){return FALSE;}
 DWORD mode=PIPE_READMODE_BYTE|PIPE_NOWAIT;if(!SetNamedPipeHandleState(channel,&mode,NULL,NULL)||!peer()){close_channel(FALSE);return FALSE;}
 uint8_t hello[56]={0};ui_put32(hello,UI_MAGIC);ui_put32(hello+4,UI_VERSION);ui_put32(hello+8,GetCurrentProcessId());ui_put64(hello+16,uurb_creation(GetCurrentProcess()));
 char secret[65];if(GetEnvironmentVariableA("UURB_FULL_BOOTSTRAP",secret,sizeof(secret))!=64){close_channel(FALSE);return FALSE;}
 for(unsigned i=0;i<32;i++){
  unsigned value=0;for(unsigned j=0;j<2;j++){char x=secret[i*2+j];unsigned digit;
   if(x>='0'&&x<='9'){digit=(unsigned)(x-'0');}else if(x>='a'&&x<='f'){digit=(unsigned)(x-'a'+10);}else{SecureZeroMemory(secret,sizeof(secret));close_channel(FALSE);return FALSE;}
   value=(value<<4)|digit;}hello[24+i]=(uint8_t)value;
 }
 SecureZeroMemory(secret,sizeof(secret));BOOL ok=transfer(hello,sizeof(hello),TRUE,deadline)&&transfer(cap,sizeof(cap),FALSE,deadline);
 SecureZeroMemory(hello,sizeof(hello));if(!ok||!valid_cap(cap)){close_channel(FALSE);return FALSE;}
 sequence=1;poisoned=FALSE;memset(pressed,0,sizeof(pressed));
 memset(&source_state,0,sizeof(source_state));source_state.version=UURB_RDP_STATE_VERSION;
 source_state.generation=ui_get64(cap+16);source_state.epoch=ui_get64(cap+72);return TRUE;
}
static void header(uint8_t* w,uint32_t operation,uint32_t bytes,uint32_t events,uint32_t units,uint64_t deadline){
 memset(w,0,UI_HEADER);ui_put32(w,UI_MAGIC);ui_put32(w+4,UI_VERSION);ui_put32(w+8,bytes);ui_put32(w+12,operation);
 ui_put64(w+16,ui_get64(cap+16));ui_put64(w+24,sequence);ui_put64(w+32,deadline);memcpy(w+40,cap+40,32);memcpy(w+72,cap+72,56);
 ui_put32(w+128,events);ui_put32(w+132,units);
}
static BOOL control(uint32_t operation,uint64_t deadline){
 uint8_t wire[UI_HEADER],reply[UI_HEADER];control_status=UI_AMBIGUOUS;header(wire,operation,UI_HEADER,0,0,deadline);
 if(operation==2){
  int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN);
  int width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if(width<2||height<2||width>16384||height>16384){return FALSE;}
  ui_put32(wire+108,(uint32_t)x);ui_put32(wire+112,(uint32_t)y);ui_put32(wire+116,(uint32_t)width);ui_put32(wire+120,(uint32_t)height);
 }
 BOOL ok=transfer(wire,sizeof(wire),TRUE,deadline)&&transfer(reply,12,FALSE,deadline);
 uint32_t bytes=ok?ui_get32(reply+8):0;
 if(!ok||(bytes!=UI_HEADER&&bytes!=UI_RECEIPT_BYTES)||!transfer(reply+12,bytes-12,FALSE,deadline)||
  ui_get32(reply)!=UI_MAGIC||ui_get32(reply+4)!=UI_VERSION||ui_get64(reply+16)!=ui_get64(cap+16)){return FALSE;}
 if(bytes==UI_RECEIPT_BYTES){
  if(ui_get64(reply+24)!=sequence||ui_get32(reply+32)!=UI_NONE||ui_get32(reply+40)||ui_get32(reply+44)){return FALSE;}
  control_status=ui_get32(reply+12);return FALSE;
 }
 if(!valid_cap(reply)||memcmp(reply+40,cap+40,32)){return FALSE;}
 memcpy(cap,reply,sizeof(cap));sequence++;control_status=UI_OK;return TRUE;
}
static BOOL configure(uint64_t deadline){
 while(clock_ns()<deadline){
  if(!control(1,deadline)){return FALSE;}
  if(control(2,deadline)){return TRUE;}
  /* Exact rejected CONFIG carries no primitives, cannot have input effects.
   * BUSY/STALE keep sequence unchanged; QUERY refreshes real geometry. */
  if(control_status!=UI_BUSY&&control_status!=UI_STALE){return FALSE;}
  Sleep(1);
 }
 return FALSE;
}
BOOL uurb_rdp_probe(DWORD* error){
 uint64_t deadline=clock_ns()+UINT64_C(500000000);
 BOOL enabled=uurb_rdp_enabled(),connected=enabled&&connect_channel(deadline);
 BOOL configured=connected&&configure(deadline);
 uint32_t required=UI_CAP_KEY|UI_CAP_MOUSE|UI_CAP_REL,caps=ui_get32(cap+128);
 if(!configured||(caps&required)!=required){
  fprintf(stderr,"UURB public readiness enabled=%u connected=%u configured=%u caps=0x%08lx missing=0x%08lx\n",
   (unsigned)enabled,(unsigned)connected,(unsigned)configured,(unsigned long)caps,(unsigned long)(required&~caps));
  *error=ERROR_NOT_READY;close_channel(FALSE);return FALSE;
 }
 *error=ERROR_SUCCESS;return TRUE;
}
BOOL uurb_rdp_semantic_idle(DWORD* error){
 if(!uurb_rdp_probe(error)){return FALSE;}
 static const unsigned modifiers[]={0x2a,0x36,0x1d,0x11d,0x38,0x138,0x15b,0x15c};
 for(unsigned i=0;i<sizeof(modifiers)/sizeof(modifiers[0]);i++){
  if(pressed[modifiers[i]]){*error=ERROR_BUSY;return FALSE;}
 }
 *error=ERROR_SUCCESS;return TRUE;
}
BOOL uurb_rdp_snapshot(uurb_rdp_state* out,DWORD* error){
 if(!uurb_rdp_enabled()||channel==INVALID_HANDLE_VALUE||!peer()||poisoned){
  memset(out,0,sizeof(*out));*error=ERROR_NOT_READY;return FALSE;
 }
 /* A QUERY refreshes ACTIVE/generation before exposing local state. */
 if(!control(1,clock_ns()+UINT64_C(100000000))){close_channel(TRUE);memset(out,0,sizeof(*out));*error=ERROR_NOT_READY;return FALSE;}
 if(source_state.epoch!=ui_get64(cap+72)){source_state.position_known=0;source_state.epoch=ui_get64(cap+72);}
 *out=source_state;*error=ERROR_SUCCESS;return TRUE;
}
static void state_key(unsigned vk,BOOL down){
 if(vk<256&&vk){source_state.keys[vk]=down?0x80:0;source_state.known[vk]=1;}
}
static void state_event(const ui_event* e){
 if(e->op==UI_ABSOLUTE){source_state.x=e->a;source_state.y=e->b;source_state.position_known=1;}
 if(e->op==UI_RELATIVE){source_state.position_known=0; /* BOOL does not witness actual relative position. */}
 if(e->op==UI_KEY){
  unsigned scan=(unsigned)e->a;if(e->flags&1u){scan|=0xe000u;}else if(e->flags&2u){return;}
  unsigned vk=MapVirtualKeyW(scan,MAPVK_VSC_TO_VK_EX);BOOL down=(e->flags&4u)!=0;
  state_key(vk,down);
  if(vk==VK_LSHIFT||vk==VK_RSHIFT){state_key(VK_SHIFT,source_state.keys[VK_LSHIFT]||source_state.keys[VK_RSHIFT]);}
  if(vk==VK_LCONTROL||vk==VK_RCONTROL){state_key(VK_CONTROL,source_state.keys[VK_LCONTROL]||source_state.keys[VK_RCONTROL]);}
  if(vk==VK_LMENU||vk==VK_RMENU){state_key(VK_MENU,source_state.keys[VK_LMENU]||source_state.keys[VK_RMENU]);}
 }
 if(e->op==UI_CURRENT_BUTTON){static const unsigned vks[5]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};state_key(vks[e->a-1],e->flags!=0);}
}
typedef struct {ui_event event;DWORD source;} mapped_event;
static BOOL add(mapped_event* out,DWORD* n,DWORD max,DWORD source,ui_event event){if(*n>=max){return FALSE;}out[*n]=(mapped_event){event,source};(*n)++;return TRUE;}
static BOOL decode_inputs(DWORD count,const INPUT* inputs,mapped_event* out,DWORD max,DWORD* n,uint16_t* text,DWORD* units,uint32_t* required,enum reject_reason* reason,DWORD* rejected_index){
 *n=0;*units=0;*required=0;uint8_t draft[1024];memcpy(draft,pressed,sizeof(draft));
 *reason=REJECT_DECODE_OTHER;*rejected_index=0;
 int32_t vx=(int32_t)ui_get32(cap+108),vy=(int32_t)ui_get32(cap+112);uint32_t width=ui_get32(cap+116),height=ui_get32(cap+120);
 for(DWORD i=0;i<count;i++){
  *rejected_index=i;
  const INPUT* in=&inputs[i];
  if(in->type==INPUT_MOUSE){
   DWORD flags=in->mi.dwFlags;const DWORD allowed=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK|MOUSEEVENTF_MOVE_NOCOALESCE|
    MOUSEEVENTF_LEFTDOWN|MOUSEEVENTF_LEFTUP|MOUSEEVENTF_RIGHTDOWN|MOUSEEVENTF_RIGHTUP|MOUSEEVENTF_MIDDLEDOWN|MOUSEEVENTF_MIDDLEUP|
    MOUSEEVENTF_XDOWN|MOUSEEVENTF_XUP|MOUSEEVENTF_WHEEL|MOUSEEVENTF_HWHEEL;
   if(flags&~allowed||((flags&MOUSEEVENTF_VIRTUALDESK)&&!(flags&MOUSEEVENTF_ABSOLUTE))||
    ((flags&(MOUSEEVENTF_XDOWN|MOUSEEVENTF_XUP))&&(flags&(MOUSEEVENTF_WHEEL|MOUSEEVENTF_HWHEEL)))){*reason=REJECT_MOUSE_FLAGS;return FALSE;}
   if(flags&MOUSEEVENTF_MOVE){
    ui_event e={0};if(flags&MOUSEEVENTF_ABSOLUTE){
     if(in->mi.dx<0||in->mi.dx>65535||in->mi.dy<0||in->mi.dy>65535){*reason=REJECT_ABSOLUTE_RANGE;return FALSE;}
     e=(ui_event){UI_ABSOLUTE,0,vx+(int32_t)(((uint64_t)in->mi.dx*(width-1)+32767)/65535),vy+(int32_t)(((uint64_t)in->mi.dy*(height-1)+32767)/65535),0,0,0,0};*required|=UI_CAP_MOUSE;
    }else{e=(ui_event){UI_RELATIVE,0,in->mi.dx,in->mi.dy,0,0,0,0};*required|=UI_CAP_REL;}
    if(!add(out,n,max,i,e)){return FALSE;}
   }
   const DWORD down[5]={MOUSEEVENTF_LEFTDOWN,MOUSEEVENTF_RIGHTDOWN,MOUSEEVENTF_MIDDLEDOWN,MOUSEEVENTF_XDOWN,MOUSEEVENTF_XDOWN};
   const DWORD up[5]={MOUSEEVENTF_LEFTUP,MOUSEEVENTF_RIGHTUP,MOUSEEVENTF_MIDDLEUP,MOUSEEVENTF_XUP,MOUSEEVENTF_XUP};
   if((flags&(MOUSEEVENTF_XDOWN|MOUSEEVENTF_XUP))&&in->mi.mouseData!=XBUTTON1&&in->mi.mouseData!=XBUTTON2){*reason=REJECT_XBUTTON_DATA;return FALSE;}
   for(unsigned button=0;button<5;button++){
    if(button>=3&&in->mi.mouseData!=(button==3?XBUTTON1:XBUTTON2)){continue;}
    if(flags&(down[button]|up[button])){*required|=UI_CAP_REL|(button<3?UI_CAP_MOUSE:UI_CAP_EXT);}
    if(flags&down[button]){if(!add(out,n,max,i,(ui_event){UI_CURRENT_BUTTON,1,(int32_t)button+1,0,0,0,0,0})){return FALSE;}}
    if(flags&up[button]){if(!add(out,n,max,i,(ui_event){UI_CURRENT_BUTTON,0,(int32_t)button+1,0,0,0,0,0})){return FALSE;}}
   }
   if(flags&(MOUSEEVENTF_WHEEL|MOUSEEVENTF_HWHEEL)){
    if((flags&(MOUSEEVENTF_WHEEL|MOUSEEVENTF_HWHEEL))==(MOUSEEVENTF_WHEEL|MOUSEEVENTF_HWHEEL)||!in->mi.mouseData){*reason=REJECT_WHEEL_DATA;return FALSE;}
    BOOL horizontal=(flags&MOUSEEVENTF_HWHEEL)!=0;*required|=UI_CAP_MOUSE|(horizontal?UI_CAP_HWHEEL:0);
    if(!add(out,n,max,i,(ui_event){UI_WHEEL,horizontal?1u:0u,(int32_t)in->mi.mouseData,vx,vy,0,0,0})){return FALSE;}
   }
  }else if(in->type==INPUT_KEYBOARD){
   const KEYBDINPUT* k=&in->ki;if(k->dwFlags&~(KEYEVENTF_EXTENDEDKEY|KEYEVENTF_KEYUP|KEYEVENTF_SCANCODE|KEYEVENTF_UNICODE)){return FALSE;}
   BOOL down=(k->dwFlags&KEYEVENTF_KEYUP)==0;
   if(k->dwFlags&KEYEVENTF_UNICODE){
    if(k->wVk||(k->dwFlags&(KEYEVENTF_SCANCODE|KEYEVENTF_EXTENDEDKEY))){return FALSE;}*required|=UI_CAP_UNICODE;
    if(k->wScan>=0xd800&&k->wScan<=0xdfff){
     if(!down||k->wScan>0xdbff||i+3>=count||*units+2>UI_TEXT_MAX){return FALSE;}
     const INPUT* b=in+1;const INPUT* c=in+2;const INPUT* d=in+3;
     if(b->type!=INPUT_KEYBOARD||c->type!=INPUT_KEYBOARD||d->type!=INPUT_KEYBOARD||
      b->ki.wVk||c->ki.wVk||d->ki.wVk||b->ki.wScan!=k->wScan||c->ki.wScan<0xdc00||c->ki.wScan>0xdfff||d->ki.wScan!=c->ki.wScan||
      b->ki.dwFlags!=(KEYEVENTF_UNICODE|KEYEVENTF_KEYUP)||c->ki.dwFlags!=KEYEVENTF_UNICODE||d->ki.dwFlags!=(KEYEVENTF_UNICODE|KEYEVENTF_KEYUP)){return FALSE;}
     text[*units]=k->wScan;text[*units+1]=c->ki.wScan;
     if(!add(out,n,max,i,(ui_event){UI_TEXT,0,(int32_t)*units,2,0,0,0,0})){return FALSE;}*units+=2;i+=3;
    }else if(!add(out,n,max,i,(ui_event){UI_UNICODE,down?1u:0u,k->wScan,0,0,0,0,0})){return FALSE;}
   }else{
    UINT scan=(k->dwFlags&KEYEVENTF_SCANCODE)?k->wScan:MapVirtualKeyW(k->wVk,MAPVK_VK_TO_VSC_EX);
    UINT prefix=scan>>8;if(!(scan&255u)||((prefix!=0)&&(prefix!=0xe0)&&(prefix!=0xe1))){return FALSE;}
    uint32_t key_flags=(down?4u:0u)|(((k->dwFlags&KEYEVENTF_EXTENDEDKEY)||prefix==0xe0)?1u:prefix==0xe1?2u:0u);
    unsigned key=(key_flags&3u)*256u+(scan&255u);
    if(down&&draft[key]){key_flags|=8u;}draft[key]=(uint8_t)down;
    *required|=UI_CAP_KEY;if(!add(out,n,max,i,(ui_event){UI_KEY,key_flags,(int32_t)(scan&255),0,0,0,0,0})){return FALSE;}
   }
  }else{return FALSE;}
 }
 if(!*n){*reason=REJECT_EMPTY;}
 return *n>0;
}
static uint64_t preflight_clock(void* unused){(void)unused;return clock_ns();}
static BOOL validate_wire(const uint8_t* wire,DWORD bytes,enum ui_status* status,BOOL* allocated){
 *allocated=FALSE;*status=UI_BUSY;
 ui_core* model=calloc(1,sizeof(*model));if(!model){return FALSE;}*allocated=TRUE;
 ui_geometry geometry={ui_get64(cap+72),ui_get32(cap+80),ui_get32(cap+84),ui_get32(cap+88),ui_get32(cap+92),ui_get32(cap+96),
  (int32_t)ui_get32(cap+100),(int32_t)ui_get32(cap+104),(int32_t)ui_get32(cap+108),(int32_t)ui_get32(cap+112),ui_get32(cap+116),ui_get32(cap+120)};
 ui_backend backend={NULL,preflight_clock,NULL,NULL,NULL};
 ui_init(model,&backend,ui_get64(cap+16),cap+40,&geometry,false);model->next_sequence=sequence;
 ui_receipt receipt;*status=ui_accept(model,wire,bytes,&receipt);BOOL valid=*status==UI_OK;
 /* This parser-only clone has no timer/worker and ui_slice is never called. */
 ui_close(model,false);ui_mark_quiesced(model,true,true);ui_destroy(model);free(model);return valid;
}
static DWORD send_inputs(DWORD count,const INPUT* inputs,DWORD* error,BOOL validate_only){
 *error=ERROR_INVALID_PARAMETER;if(!inputs||!count||count>2048){return 0;}
 uint64_t deadline=clock_ns()+UINT64_C(500000000);
 if(!connect_channel(deadline)||!configure(deadline)){*error=ERROR_NOT_READY;close_channel(TRUE);return 0;}
 DWORD max=count*12,n=0,units=0;uint32_t required=0;
 mapped_event* events=calloc(max,sizeof(*events));uint16_t* text=calloc(UI_TEXT_MAX,sizeof(*text));uint8_t* wire=calloc(UI_WIRE_MAX,1);
 if(!events||!text||!wire){free(events);free(text);free(wire);*error=ERROR_NOT_ENOUGH_MEMORY;return 0;}
 DWORD result=0;
 enum reject_reason reason=REJECT_DECODE_OTHER;DWORD rejected_index=0;
 BOOL decoded=decode_inputs(count,inputs,events,max,&n,text,&units,&required,&reason,&rejected_index);
 if(!decoded||(ui_get32(cap+128)&required)!=required){
  *error=ERROR_NOT_SUPPORTED;
  rejection_diagnostic(decoded?REJECT_MISSING_CAPS:reason,count,inputs,rejected_index,required,decoded,FALSE,UI_AMBIGUOUS,deadline);
  goto done;
 }
 for(unsigned pass=0;pass<2;pass++){
 if(pass==1&&validate_only){result=count;*error=ERROR_SUCCESS;goto done;}
 for(DWORD offset=0;offset<n;){
  DWORD batch=n-offset>UI_EVENTS_MAX?UI_EVENTS_MAX:n-offset;
  DWORD bytes=UI_HEADER+batch*UI_EVENT_BYTES+units*2;
  if(bytes>UI_WIRE_MAX||clock_ns()>=deadline){*error=ERROR_TIMEOUT;goto failed;}
  header(wire,0,bytes,batch,units,deadline);
  for(DWORD j=0;j<batch;j++){
   const ui_event* e=&events[offset+j].event;uint8_t* at=wire+UI_HEADER+j*UI_EVENT_BYTES;
   ui_put32(at,e->op);ui_put32(at+4,e->flags);ui_put32(at+8,(uint32_t)e->a);ui_put32(at+12,(uint32_t)e->b);ui_put32(at+16,(uint32_t)e->c);ui_put32(at+20,(uint32_t)e->d);ui_put32(at+24,e->aux);ui_put32(at+28,e->reserved);
  }
  for(DWORD j=0;j<units;j++){wire[UI_HEADER+batch*UI_EVENT_BYTES+j*2]=(uint8_t)text[j];wire[UI_HEADER+batch*UI_EVENT_BYTES+j*2+1]=(uint8_t)(text[j]>>8);}
  if(pass==0){enum ui_status status;BOOL allocated;
   if(!validate_wire(wire,bytes,&status,&allocated)){
    *error=ERROR_NOT_SUPPORTED;
    rejection_diagnostic(allocated?REJECT_PREFLIGHT_STATUS:REJECT_PREFLIGHT_ALLOC,count,inputs,events[offset].source,required,TRUE,TRUE,status,deadline);
    goto done;
   }offset+=batch;continue;}
  uint8_t reply[UI_RECEIPT_BYTES];
  if(!transfer(wire,bytes,TRUE,deadline)||!transfer(reply,sizeof(reply),FALSE,deadline)||ui_get32(reply)!=UI_MAGIC||ui_get32(reply+4)!=UI_VERSION||ui_get32(reply+8)!=sizeof(reply)||
   ui_get64(reply+16)!=ui_get64(cap+16)||ui_get64(reply+24)!=sequence){*error=ERROR_TIMEOUT;goto failed;}
  if(ui_get32(reply+12)!=UI_OK||ui_get32(reply+32)<UI_WRAPPER_ATTEMPTED){
   DWORD source=ui_get32(reply+36);result=source<batch?events[offset+source].source:result;
   *error=ERROR_WRITE_FAULT;goto failed;
  }
  for(DWORD j=0;j<batch;j++){
   const ui_event* event=&events[offset+j].event;
   state_event(event);
   if(event->op==UI_KEY){pressed[(event->flags&3u)*256u+(unsigned)event->a]=(uint8_t)((event->flags&4u)!=0);}
  }
  source_state.epoch=ui_get64(cap+72);sequence++;offset+=batch;
  result=offset==n?count:events[offset].source;
 }
 }
 *error=ERROR_SUCCESS;goto done;
failed:
 close_channel(TRUE); /* Never replay accepted/ambiguous events or fall back to foreground. */
done:
 SecureZeroMemory(text,UI_TEXT_MAX*sizeof(*text));SecureZeroMemory(wire,UI_WIRE_MAX);free(events);free(text);free(wire);return result;
}

DWORD uurb_rdp_send(DWORD count,const INPUT* inputs,DWORD* error){return send_inputs(count,inputs,error,FALSE);}
BOOL uurb_rdp_validate(DWORD count,const INPUT* inputs,DWORD* error){return send_inputs(count,inputs,error,TRUE)==count&&*error==ERROR_SUCCESS;}
