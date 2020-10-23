#!/bin/sh
gcc -Wall -Wextra -Wno-unused -g -O0 `pkg-config --cflags --libs ncursesw expat` -o tml2tty tml2tty.c
