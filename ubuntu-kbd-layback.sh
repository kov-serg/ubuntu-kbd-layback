#!/bin/sh

gcc -o ubuntu-kbd-layback ubuntu-kbd-layback.c `pkg-config --cflags --libs gio-2.0` `pkg-config --cflags --libs x11` && \
./ubuntu-kbd-layback 5
