#include "freerdp-adapter.h"
#include <string.h>
#include <wchar.h>
static bool resolve(HMODULE module,const char* name,void* target,size_t bytes) {
 FARPROC value=GetProcAddress(module,name);
 if(!value || bytes!=sizeof(value)){ return false; }
 memcpy(target,&value,bytes);return true;
}
#define R(field,name) resolve(main,name,&out->field,sizeof(out->field))
static bool resolve_exports(ui_exports* out,HMODULE main) {
 memset(out,0,sizeof(*out));
 return R(key,"freerdp_input_send_keyboard_event") &&
 R(key_ex,"freerdp_input_send_keyboard_event_ex") &&
 R(pause,"freerdp_input_send_keyboard_pause_event") &&
 R(unicode,"freerdp_input_send_unicode_keyboard_event") &&
 R(mouse,"freerdp_input_send_mouse_event") &&
 R(extended,"freerdp_input_send_extended_mouse_event") &&
 R(relative,"freerdp_input_send_rel_mouse_event") &&
 R(synchronize,"freerdp_input_send_synchronize_event") &&
 R(focus,"freerdp_input_send_focus_in_event") &&
 R(qoe,"freerdp_input_send_qoe_timestamp") &&
 R(active,"freerdp_is_active_state") &&
 R(boolean,"freerdp_settings_get_bool") &&
 R(integer,"freerdp_settings_get_uint32") &&
 R(timer_add,"freerdp_timer_add") && R(timer_remove,"freerdp_timer_remove");
}
#undef R
static bool colocated_core(HMODULE main,HMODULE core) {
 WCHAR executable[MAX_PATH],library[MAX_PATH];
 DWORD executable_length=GetModuleFileNameW(main,executable,MAX_PATH);
 DWORD library_length=GetModuleFileNameW(core,library,MAX_PATH);
 if(!executable_length||executable_length>=MAX_PATH||!library_length||library_length>=MAX_PATH){return false;}
 WCHAR* executable_end=wcsrchr(executable,L'\\');
 WCHAR* library_end=wcsrchr(library,L'\\');
 if(!executable_end||!library_end||executable_end-executable!=library_end-library){return false;}
 return _wcsnicmp(executable,library,(size_t)(executable_end-executable)+1)==0;
}
bool ui_resolve(ui_exports* out,HMODULE main) {
 if(resolve_exports(out,main)){return true;}
 /* The pinned shared build exports its API from the already loaded core DLL.
  * Never search for or load a replacement, and require the executable's directory. */
 HMODULE core=GetModuleHandleW(L"libfreerdp3.dll");
 if(main==GetModuleHandleW(NULL)&&core&&colocated_core(main,core)&&resolve_exports(out,core)){return true;}
 memset(out,0,sizeof(*out));return false;
}
bool ui_rdp_live(const ui_exports* e,rdpContext* c,ui_live* out,const ui_geometry* geometry) {
 memset(out,0,sizeof(*out));
 if(!c || !c->input || c->input->context!=c || !c->settings){ return false; }
 out->associated=true;out->active=e->active(c)!=FALSE;
 out->suspended=e->boolean(c->settings,FreeRDP_SuspendInput)!=FALSE;
 out->geometry=*geometry;
 out->geometry.width=e->integer(c->settings,FreeRDP_DesktopWidth);
 out->geometry.height=e->integer(c->settings,FreeRDP_DesktopHeight);
 if(c->input->KeyboardEvent){ out->caps|=UI_CAP_KEY; }
 if(c->input->UnicodeKeyboardEvent && e->boolean(c->settings,FreeRDP_UnicodeInput)){ out->caps|=UI_CAP_UNICODE; }
 if(c->input->MouseEvent){ out->caps|=UI_CAP_MOUSE; }
 if(c->input->ExtendedMouseEvent && e->boolean(c->settings,FreeRDP_HasExtendedMouseEvent)){ out->caps|=UI_CAP_EXT; }
 if(c->input->RelMouseEvent && e->boolean(c->settings,FreeRDP_HasRelativeMouseEvent)){ out->caps|=UI_CAP_REL; }
 if((out->caps&UI_CAP_MOUSE) && e->boolean(c->settings,FreeRDP_HasHorizontalWheel)){ out->caps|=UI_CAP_HWHEEL; }
 if(c->input->SynchronizeEvent){ out->caps|=UI_CAP_SYNC; }
 if(c->input->KeyboardPauseEvent){ out->caps|=UI_CAP_PAUSE; }
 if(c->input->FocusInEvent){ out->caps|=UI_CAP_FOCUS; }
 if(c->input->QoEEvent){ out->caps|=UI_CAP_QOE; }
 /* No real clipboard ownership/recipient provider is linked in this source lane. */
 return true;
}
bool ui_rdp_send(const ui_exports* e,rdpContext* c,const ui_primitive* p) {
 if(!c || !c->input || c->input->context!=c){ return false; }
 switch(p->op){
 case UI_KEY:{
  UINT16 flags=(p->flags&1u?KBD_FLAGS_EXTENDED:0)|(p->flags&2u?KBD_FLAGS_EXTENDED1:0);
  if(!(p->flags&4u)){ flags|=KBD_FLAGS_RELEASE; }
  else if(p->flags&8u){ flags|=KBD_FLAGS_DOWN; }
  /* E1 is represented by the actual public flags API. E0/nonextended use Ex. */
  if(p->flags&2u){ return e->key(c->input,flags,(UINT8)p->a)!=FALSE; }
  UINT32 scan=MAKE_RDP_SCANCODE((UINT32)p->a,(p->flags&1u)!=0);
  return e->key_ex(c->input,(p->flags&4u)!=0,(p->flags&8u)!=0,scan)!=FALSE;}
 case UI_UNICODE:return e->unicode(c->input,p->flags?0:KBD_FLAGS_RELEASE,(UINT16)p->a)!=FALSE;
 case UI_ABSOLUTE:return e->mouse(c->input,PTR_FLAGS_MOVE,(UINT16)p->a,(UINT16)p->b)!=FALSE;
 case UI_BUTTON:{
  unsigned button=p->flags&255u;bool down=(p->flags&256u)!=0;
  if(button<=3){UINT16 flags=(UINT16)(0x1000u<<(button-1));if(down){ flags|=PTR_FLAGS_DOWN; }
   return e->mouse(c->input,flags,(UINT16)p->a,(UINT16)p->b)!=FALSE;}
  UINT16 flags=(UINT16)(button==4?PTR_XFLAGS_BUTTON1:PTR_XFLAGS_BUTTON2);
  if(down){ flags|=PTR_XFLAGS_DOWN; }
  return e->extended(c->input,flags,(UINT16)p->a,(UINT16)p->b)!=FALSE;}
 case UI_RELATIVE:{
  /* Internal cleanup flags1..5 mean button release at current position;
   * normal decoded relative motion always has flags0. */
  UINT16 flags=PTR_FLAGS_MOVE;
  if(p->flags){
   unsigned button=p->flags&255u;
   if(button<1||button>5||(p->flags&~511u)||p->a||p->b){return false;}
   flags=button<=3?(UINT16)(0x1000u<<(button-1u)):
    (UINT16)(button==4?PTR_XFLAGS_BUTTON1:PTR_XFLAGS_BUTTON2);
   if(p->flags&256u){flags|=PTR_FLAGS_DOWN;}
  }
  return e->relative(c->input,flags,(INT16)p->a,(INT16)p->b)!=FALSE;}
 case UI_WHEEL:return e->mouse(c->input,(UINT16)p->flags,(UINT16)p->a,(UINT16)p->b)!=FALSE;
 case UI_SYNC:return e->synchronize(c->input,(UINT32)p->a)!=FALSE;
 case UI_FOCUS:return e->focus(c->input,(UINT16)p->a)!=FALSE;
 case UI_PAUSE:return e->pause(c->input)!=FALSE;
 case UI_QOE:return e->qoe(c->input,(UINT32)p->a)!=FALSE;
 default:return false;
 }
}
