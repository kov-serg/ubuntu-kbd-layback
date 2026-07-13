// ubuntu-kbd-layback.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>

const char *kbmon_stop_key="Scroll_Lock";

static int kbd_get_layout_index(int *group) {
    int rc=0; Display *dpy; XkbDescPtr kbd=0; XkbStateRec state[1];
    dpy=XOpenDisplay(0);    if (!dpy) { rc=1; goto leave; }
    kbd=XkbAllocKeyboard(); if (!kbd) { rc=2; goto leave; }
    kbd->dpy=dpy;
    kbd->device_spec=XkbUseCoreKbd;
    if (XkbGetState(dpy, kbd->device_spec, state)) { rc=3; goto leave; }
    if (group) *group=state->group;
leave:
    if (kbd) XkbFreeKeyboard(kbd, XkbGroupNamesMask, True);
    if (dpy) XCloseDisplay(dpy);
    return rc;
}

static int kbd_set_layout_index(int index) {
    Display *dpy=XOpenDisplay(0); if (!dpy) return 1;
    XkbLockGroup(dpy, XkbUseCoreKbd, index);
    XCloseDisplay(dpy);
    return 0;
}

enum { kbm_map_size=32 };
typedef struct KbdMonitor {
    Display *display; XkbDescRec* kbd;
    char map[2][kbm_map_size];
    int tmr1,tmr1_en,tmr1_limit;
    int quit;
    int (*set_layout)(int layout);
    int (*get_layout)(int *layout);
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
    XQueryKeymap(p->display,p->map[1]);
leave:
    if (rc) kbmon_done(p);
    return rc;
}


static int is_key_pressed(KbdMonitor* p,const char* name) {
    KeySym ks; KeyCode kc; int pos,mask;
    ks=XStringToKeysym(name);
    if (ks==NoSymbol) return 0;
    kc=XKeysymToKeycode(p->display,ks);
    pos=kc>>3; mask=1<<(kc&7);
    return p->map[1][pos]&mask;
}

static void kbmon_step(KbdMonitor* p) {
    int idx;
    memcpy(p->map[0],p->map[1],kbm_map_size);
    XQueryKeymap(p->display,p->map[1]);
    if (p->tmr1_en) p->tmr1++;
    if (memcmp(p->map[0],p->map[1],kbm_map_size)!=0) {
        p->tmr1=0; p->tmr1_en=1;
    }
    if (is_key_pressed(p,kbmon_stop_key)) { p->quit=1; }
    if (p->tmr1_en && p->tmr1>=p->tmr1_limit) { p->tmr1_en=0;
        idx=0; p->get_layout(&idx);
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
    printf("start monitoring keyboard tau=%.1fs - press %s to exit\n",
        km->tmr1_limit*0.1, kbmon_stop_key);
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
