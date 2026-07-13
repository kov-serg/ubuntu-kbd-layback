#!/bin/sh

gcc -o ubuntu-kbd-layback ubuntu-kbd-layback.c -lX11 && \
./ubuntu-kbd-layback 5
