/* wmclose WINDOW_ID: asks a window to close the way a window manager does
   (WM_DELETE_WINDOW), like Super+Q on Hyprland or the title bar's X. */
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    Display* d = XOpenDisplay(NULL);
    if (!d) return 1;
    Window w = (Window)strtoul(argv[1], NULL, 0);
    XEvent e = {0};
    e.xclient.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = XInternAtom(d, "WM_PROTOCOLS", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = (long)XInternAtom(d, "WM_DELETE_WINDOW", False);
    e.xclient.data.l[1] = CurrentTime;
    XSendEvent(d, w, False, NoEventMask, &e);
    XFlush(d);
    XCloseDisplay(d);
    return 0;
}
