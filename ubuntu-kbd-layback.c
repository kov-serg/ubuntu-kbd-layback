// ubuntu-kbd-layback.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <gio/gio.h>    // pkg-config --cflags --libs gio-2.0
#include <X11/Xlib.h>   // pkg-config --cflags --libs x11
#include <X11/XKBlib.h>

static int gshell_run_script(const gchar *script,gchar **result) {
	enum { timeout=500 }; GDBusConnection *con=0; GVariantBuilder builder[1];
	GVariant *vres=0,*vscript=0,*param=0; gboolean success; int rc=0;
	const gchar *bus_name="org.gnome.Shell", *object_path="/org/gnome/Shell",
		  *interface_name="org.gnome.Shell", *method_name="Eval";
	con=g_bus_get_sync(G_BUS_TYPE_SESSION,0,0); if (!con) { rc=1; goto leave; }
	g_variant_builder_init(builder,G_VARIANT_TYPE_TUPLE);
	vscript=g_variant_parse(0,script,0,0,0); if (!vscript) { rc=2;goto leave; }
	g_variant_builder_add_value(builder,vscript);
	param=g_variant_builder_end(builder); if (!param) { rc=3; goto leave; }
	vres=g_dbus_connection_call_sync(con,bus_name,object_path,interface_name,
		method_name,param,0,G_DBUS_CALL_FLAGS_NONE,timeout,0,0);
	if (!vres) { rc=4; goto leave; }
	if (!g_variant_is_of_type(vres,G_VARIANT_TYPE("(bs)"))) {rc=5;goto leave;}
	g_variant_get(vres,"(bs)",&success,result);
	if (!success ) { rc=6; goto leave; }
leave:
	if (rc!=0) { g_free(*result); *result=0; }
	if (vres) { g_variant_unref(vres); vres=0; }
	if (vscript) { g_variant_unref(vscript); vscript=0; }
	if (con) { g_object_unref(con); con=0; }
	return rc;
}

static int kbd_get_layout_index(int index_if_error) {
	gchar *result=0; int res=index_if_error,ret;
	const char* js="\"imports.ui.status.keyboard.getInputSourceManager()"
		".currentSource.index\"";
	ret=gshell_run_script(js,&result);
	if (result) { res=atoi(result); g_free(result); }
	return res;
}

static int kbd_set_layout_index(int index) {
	const char* jst="\"imports.ui.status.keyboard.getInputSourceManager()"
		".inputSources[%d].activate()\"";
	enum { js_size=100 }; char js[js_size]; sprintf(js,jst,index);
	return gshell_run_script(js,0);
}

static int kbd_get_layout_index_old(int index_if_error) {
	int rc=index_if_error; GSettings *gs; GVariant *v;
	gs=g_settings_new("org.gnome.desktop.input-sources");
	if (gs) {
		rc=g_settings_get_uint(gs,"current");
		g_object_unref(gs);
	}
	return rc;
}

static int kbd_set_layout_index_old(int index) {
	int rc; GSettings *gs;
	gs=g_settings_new("org.gnome.desktop.input-sources"); if (!gs) return 1;
	rc=g_settings_set_uint(gs,"current",index);
	g_object_unref(gs);
	g_settings_sync();
	return rc ? 0 : 2;
}

enum { kbm_map_size=32 };
typedef struct KbdMonitor {
	Display *display; XkbDescRec* kbd;
	char map[2][kbm_map_size];
	int tmr1,tmr1_en,tmr1_limit;
	int quit;
	int (*set_layout)(int idx);
	int (*get_layout)(int idx_if_err);
} KbdMonitor;

static void kbmon_done(KbdMonitor* p) {
	if (p->kbd) { XkbFreeKeyboard(p->kbd,0,True); p->kbd=0; }
	if (p->display) { XCloseDisplay(p->display); p->display=0; }
	p->quit=1;
}

static int kbmon_init(KbdMonitor* p) {
	int rc=0;
	Atom sym_name;char *layoutString;
	p->kbd=0;
	p->tmr1=0;
	p->tmr1_en=1;
	p->tmr1_limit=50; // 5sec
	p->quit=0;
	p->set_layout=kbd_set_layout_index;
	p->get_layout=kbd_get_layout_index;
	p->display=XOpenDisplay(0); if (!p->display) { rc=1; goto leave; }
	p->kbd=XkbAllocKeyboard(); if (!p->kbd) { rc=2; goto leave; }
	if (kbd_get_layout_index(-1)<0) {
		p->set_layout=kbd_set_layout_index_old;
		p->get_layout=kbd_get_layout_index_old;
	}
	XQueryKeymap(p->display,p->map[1]);
leave:
	if (rc) kbmon_done(p);
	return rc;
}

static void kbmon_step(KbdMonitor* p) {
	int idx;
	memcpy(p->map[0],p->map[1],kbm_map_size);
	XQueryKeymap(p->display,p->map[1]);
	if (p->tmr1_en) p->tmr1++;
	if (memcmp(p->map[0],p->map[1],kbm_map_size)!=0) {
		p->tmr1=0; p->tmr1_en=1; 
	}
	if (p->map[1][15]&128) { p->quit=1; } // pause key
	if (p->tmr1_en && p->tmr1>=p->tmr1_limit) { p->tmr1_en=0;
		idx=p->get_layout(0);
		if (idx) {
			printf("set default layout\n");
			p->set_layout(0);
		} else { 
			p->tmr1=0; p->tmr1_en=1; 
		}
	}
}

static void set_echo(int en) {
	struct termios term;
	tcgetattr(fileno(stdin), &term);
	if (en) term.c_lflag |= ECHO;
	else term.c_lflag &= ~ECHO;
	tcsetattr(fileno(stdin), 0, &term);
}

int main(int argc,char** argv) {
	KbdMonitor km[1];int rc;

	rc=kbmon_init(km); if (rc) return rc;
	if (argc>1) {
		int x=atoi(argv[1]);
		if (x>0 && x<=3600) km->tmr1_limit=x*10;
		else { fprintf(stderr,"invalid value\n"); return -1; }
	}
	printf("start monitoring keyboard tau=%.1fs - press PAUSE to exit\n",
		km->tmr1_limit*0.1);
	set_echo(0);
	while(!km->quit) {
		kbmon_step(km);
		usleep(100000);
	}
	set_echo(1);
	kbmon_done(km);
	printf("stop monitoring keyboard\n");
	return 0;
}
