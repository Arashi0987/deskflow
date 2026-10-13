/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 *
 * Simulation helper: define RandR 1.5 monitors on an Xvfb display and keep the
 * connection open (the X server drops monitors when their creator disconnects).
 *
 * usage: fake-monitors NAME=WxH+X+Y [NAME=WxH+X+Y ...]
 */
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
  Display *display = XOpenDisplay(NULL);
  if (display == NULL) {
    fprintf(stderr, "cannot open display\n");
    return 1;
  }
  Window root = DefaultRootWindow(display);

  // claiming the real output in the first monitor hides the automatic monitor for it
  XRRScreenResources *resources = XRRGetScreenResourcesCurrent(display, root);
  RROutput output = (resources != NULL && resources->noutput > 0) ? resources->outputs[0] : None;
  for (int i = 1; i < argc; ++i) {
    char name[64];
    int w, h, x, y;
    if (sscanf(argv[i], "%63[^=]=%dx%d+%d+%d", name, &w, &h, &x, &y) != 5) {
      fprintf(stderr, "bad monitor spec: %s\n", argv[i]);
      return 2;
    }
    XRRMonitorInfo *monitor = XRRAllocateMonitor(display, (i == 1 && output != None) ? 1 : 0);
    if (i == 1 && output != None) {
      monitor->outputs[0] = output;
    }
    monitor->name = XInternAtom(display, name, False);
    monitor->primary = i == 1;
    monitor->x = x;
    monitor->y = y;
    monitor->width = w;
    monitor->height = h;
    monitor->mwidth = w * 254 / 960;
    monitor->mheight = h * 254 / 960;
    XRRSetMonitor(display, root, monitor);
    XFree(monitor);
  }
  XSync(display, False);
  printf("monitors defined\n");
  fflush(stdout);
  for (;;) {
    pause();
  }
}
