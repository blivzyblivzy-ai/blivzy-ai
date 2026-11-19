#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define UNUSED(x) (void)(x)
/* strlcpy/strlcat */
/* FuriString */
typedef struct{char*s;size_t n,cap;}FuriString;
static inline void fs_need(FuriString*f,size_t n){if(n+1>f->cap){f->cap=(n+1)*2;f->s=realloc(f->s,f->cap);}}
static inline FuriString*furi_string_alloc(void){FuriString*f=calloc(1,sizeof(*f));fs_need(f,16);f->s[0]=0;return f;}
static inline void furi_string_free(FuriString*f){free(f->s);free(f);}
static inline const char*furi_string_get_cstr(FuriString*f){return f->s;}
static inline size_t furi_string_size(FuriString*f){return f->n;}
static inline char furi_string_get_char(FuriString*f,size_t i){return f->s[i];}
static inline void furi_string_reset(FuriString*f){f->n=0;f->s[0]=0;}
static inline void furi_string_left(FuriString*f,size_t n){if(n<f->n){f->n=n;f->s[n]=0;}}
static inline void furi_string_set_str(FuriString*f,const char*x){size_t l=strlen(x);char*t=strdup(x);fs_need(f,l);memcpy(f->s,t,l+1);f->n=l;free(t);}
static inline void furi_string_set(FuriString*f,FuriString*g){furi_string_set_str(f,g->s);}
static inline void furi_string_cat_str(FuriString*f,const char*x){size_t l=strlen(x);fs_need(f,f->n+l);memcpy(f->s+f->n,x,l+1);f->n+=l;}
static inline void furi_string_cat(FuriString*f,FuriString*g){furi_string_cat_str(f,g->s);}
static inline void furi_string_push_back(FuriString*f,char c){fs_need(f,f->n+1);f->s[f->n++]=c;f->s[f->n]=0;}
static inline void furi_string_vcat(FuriString*f,const char*fmt,va_list ap){char b[4096];vsnprintf(b,sizeof b,fmt,ap);furi_string_cat_str(f,b);}
static inline void furi_string_cat_printf(FuriString*f,const char*fmt,...){va_list ap;va_start(ap,fmt);furi_string_vcat(f,fmt,ap);va_end(ap);}
static inline void furi_string_printf(FuriString*f,const char*fmt,...){char b[4096];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);furi_string_set_str(f,b);}
static inline FuriString*furi_string_alloc_printf(const char*fmt,...){char b[4096];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof b,fmt,ap);va_end(ap);FuriString*f=furi_string_alloc();furi_string_set_str(f,b);return f;}
/* hal */
typedef struct{uint8_t hour,minute,second,day,month,weekday;uint16_t year;}DateTime;
static inline void furi_hal_rtc_get_datetime(DateTime*d){d->hour=14;d->minute=7;d->second=9;d->day=18;d->month=11;d->year=2026;d->weekday=3;}
static inline uint8_t furi_hal_power_get_pct(void){return 87;}
static inline uint32_t furi_hal_random_get(void){return (uint32_t)rand();}
static inline bool furi_hal_speaker_acquire(uint32_t t){(void)t;return true;}
static inline void furi_hal_speaker_start(float f,float v){(void)f;(void)v;}
static inline void furi_hal_speaker_stop(void){}
static inline void furi_hal_speaker_release(void){}
static inline void furi_delay_ms(uint32_t m){(void)m;}
/* storage -> maps /ext/ to $SD */
#define RECORD_STORAGE "s"
#define RECORD_GUI "g"
typedef struct{FILE*f;}File; typedef struct{int x;}Storage;
enum{FSAM_READ,FSAM_WRITE};enum{FSOM_OPEN_EXISTING,FSOM_OPEN_APPEND};
static inline void*furi_record_open(const char*n){(void)n;static Storage s;return &s;}
static inline void furi_record_close(const char*n){(void)n;}
static inline File*storage_file_alloc(Storage*s){(void)s;return calloc(1,sizeof(File));}
static inline bool storage_file_open(File*f,const char*p,int a,int m){(void)m;char b[512];snprintf(b,sizeof b,"%s/%s",getenv("SD"),p+5);f->f=fopen(b,a==FSAM_READ?"rb":"ab");return f->f!=NULL;}
static inline size_t storage_file_read(File*f,void*b,size_t n){return fread(b,1,n,f->f);}
static inline size_t storage_file_write(File*f,const void*b,size_t n){return fwrite(b,1,n,f->f);}
static inline void storage_file_close(File*f){if(f->f)fclose(f->f);f->f=NULL;}
static inline void storage_file_free(File*f){free(f);}
static inline void storage_simply_mkdir(Storage*s,const char*p){(void)s;(void)p;}
/* gui stubs */
typedef struct Canvas Canvas; typedef struct{int key,type;}InputEvent;
enum{InputKeyUp,InputKeyDown,InputKeyRight,InputKeyLeft,InputKeyOk,InputKeyBack};
enum{InputTypeShort,InputTypeLong,InputTypeRepeat};
enum{ColorWhite,ColorBlack};enum{FontSecondary};enum{AlignCenter,AlignBottom};enum{TextBoxFontText};
enum{ViewModelTypeLocking};
#define VIEW_NONE 0xFFFFFFFF
typedef struct{void*model;}View;typedef struct{int x;}ViewDispatcher;typedef struct{int x;}Submenu;typedef struct{int x;}TextBox;typedef struct{int x;}Gui;
#define with_view_model(v,decl,code,upd) {decl=(void*)(v)->model;code}
static inline void canvas_draw_line(Canvas*c,int a,int b,int d,int e){(void)c;(void)a;(void)b;(void)d;(void)e;}static inline void canvas_draw_dot(Canvas*c,int a,int b){(void)c;(void)a;(void)b;}
static inline void canvas_clear(Canvas*c){(void)c;}static inline void canvas_set_color(Canvas*c,int x){(void)c;(void)x;}static inline void canvas_set_font(Canvas*c,int x){(void)c;(void)x;}
static inline void canvas_draw_rbox(Canvas*c,int a,int b,int d,int e,int f){(void)c;(void)a;(void)b;(void)d;(void)e;(void)f;}
static inline void canvas_draw_rframe(Canvas*c,int a,int b,int d,int e,int f){(void)c;(void)a;(void)b;(void)d;(void)e;(void)f;}
static inline void canvas_draw_frame(Canvas*c,int a,int b,int d,int e){(void)c;(void)a;(void)b;(void)d;(void)e;}
static inline void canvas_draw_str(Canvas*c,int a,int b,const char*s){(void)c;(void)a;(void)b;(void)s;}
static inline void canvas_draw_glyph(Canvas*c,int a,int b,uint16_t s){(void)c;(void)a;(void)b;(void)s;}
static inline void canvas_draw_xbm(Canvas*c,int a,int b,int d,int e,const uint8_t*s){(void)c;(void)a;(void)b;(void)d;(void)e;(void)s;}
static inline void canvas_draw_str_aligned(Canvas*c,int a,int b,int x,int y,const char*s){(void)c;(void)a;(void)b;(void)x;(void)y;(void)s;}
static inline int canvas_string_width(Canvas*c,const char*s){(void)c;return (int)strlen(s)*5;}
static inline View*view_alloc(void){return calloc(1,sizeof(View));}
static inline void view_set_context(View*v,void*c){(void)v;(void)c;}
static inline void view_set_draw_callback(View*v,void*c){(void)v;(void)c;}
static inline void view_set_input_callback(View*v,void*c){(void)v;(void)c;}
static inline void view_set_previous_callback(View*v,void*c){(void)v;(void)c;}
static inline void view_allocate_model(View*v,int t,size_t n){(void)t;v->model=calloc(1,n);}
static inline void view_free_model(View*v){free(v->model);}
static inline void view_free(View*v){free(v);}
static inline ViewDispatcher*view_dispatcher_alloc(void){return calloc(1,sizeof(ViewDispatcher));}
static inline void view_dispatcher_set_event_callback_context(ViewDispatcher*v,void*c){(void)v;(void)c;}
static inline void view_dispatcher_set_custom_event_callback(ViewDispatcher*v,void*c){(void)v;(void)c;}
static inline void view_dispatcher_add_view(ViewDispatcher*v,int i,View*w){(void)v;(void)i;(void)w;}
static inline void view_dispatcher_remove_view(ViewDispatcher*v,int i){(void)v;(void)i;}
static inline void view_dispatcher_attach_to_gui(ViewDispatcher*v,Gui*g,int t){(void)v;(void)g;(void)t;}
static inline void view_dispatcher_switch_to_view(ViewDispatcher*v,int i){(void)v;(void)i;}
static inline void view_dispatcher_send_custom_event(ViewDispatcher*v,uint32_t e){(void)v;(void)e;}
static inline void view_dispatcher_run(ViewDispatcher*v){(void)v;}
static inline void view_dispatcher_free(ViewDispatcher*v){free(v);}
#define ViewDispatcherTypeFullscreen 0
static inline Submenu*submenu_alloc(void){return calloc(1,sizeof(Submenu));}
static inline View*submenu_get_view(Submenu*s){(void)s;return view_alloc();}
static inline void submenu_reset(Submenu*s){(void)s;}static inline void submenu_set_header(Submenu*s,const char*h){(void)s;(void)h;}
static inline void submenu_add_item(Submenu*s,const char*l,uint32_t i,void*cb,void*c){(void)s;(void)l;(void)i;(void)cb;(void)c;}
static inline void submenu_set_selected_item(Submenu*s,uint32_t i){(void)s;(void)i;}static inline void submenu_free(Submenu*s){free(s);}
static inline TextBox*text_box_alloc(void){return calloc(1,sizeof(TextBox));}static inline View*text_box_get_view(TextBox*t){(void)t;return view_alloc();}
static inline void text_box_reset(TextBox*t){(void)t;}static inline void text_box_set_font(TextBox*t,int f){(void)t;(void)f;}
static inline void text_box_set_text(TextBox*t,const char*s){(void)t;(void)s;}static inline void text_box_free(TextBox*t){free(t);}
static inline void*furi_record_open_gui(void){return calloc(1,sizeof(Gui));}
