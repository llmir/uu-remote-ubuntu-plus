#ifndef UURB_FREERDP_ADAPTER_H
#define UURB_FREERDP_ADAPTER_H
#include <winpr/windows.h>
#include <freerdp/dvc.h>
#include <freerdp/freerdp.h>
#include <freerdp/input.h>
#include <freerdp/scancode.h>
#include <freerdp/timer.h>
#include "full-input.h"
/* Actual source header types; resolve the loaded main or its colocated core DLL. */
typedef struct {
 BOOL (*key)(rdpInput*,UINT16,UINT8);
 BOOL (*key_ex)(rdpInput*,BOOL,BOOL,UINT32);
 BOOL (*pause)(rdpInput*);
 BOOL (*unicode)(rdpInput*,UINT16,UINT16);
 BOOL (*mouse)(rdpInput*,UINT16,UINT16,UINT16);
 BOOL (*extended)(rdpInput*,UINT16,UINT16,UINT16);
 BOOL (*relative)(rdpInput*,UINT16,INT16,INT16);
 BOOL (*synchronize)(rdpInput*,UINT32);
 BOOL (*focus)(rdpInput*,UINT16);
 BOOL (*qoe)(rdpInput*,UINT32);
 BOOL (*active)(const rdpContext*);
 BOOL (*boolean)(const rdpSettings*,FreeRDP_Settings_Keys_Bool);
 UINT32 (*integer)(const rdpSettings*,FreeRDP_Settings_Keys_UInt32);
 FreeRDP_TimerID (*timer_add)(rdpContext*,uint64_t,FreeRDP_TimerCallback,void*,bool);
 bool (*timer_remove)(rdpContext*,FreeRDP_TimerID);
} ui_exports;
bool ui_resolve(ui_exports*,HMODULE);
bool ui_rdp_live(const ui_exports*,rdpContext*,ui_live*,const ui_geometry*);
bool ui_rdp_send(const ui_exports*,rdpContext*,const ui_primitive*);
#endif
