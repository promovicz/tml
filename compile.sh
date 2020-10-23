#!/bin/sh

CFLAGS=
LINK=

log() {
  echo $@
  $@
}

pkg() {
  echo -n "Looking for $1..."
  if pkg-config --exists $1; then
    echo "found."
    CFLAGS="$CFLAGS $(pkg-config --cflags $1)"
    LINK="$LINK $(pkg-config --libs $1)"
    return 0
  else
    echo "not found."
    return 1
  fi
}

if ! pkg expat; then
  echo "Need expat installed."
  exit 1
fi

if pkg ncursesw; then
  echo "Using ncurses with WCS."
elif pkg ncurses; then
  echo "Using ncurses without WCS."
else
  echo "Need ncursesw or ncurses installed."
  exit 1
fi

echo ""

log gcc -Wall -Wextra -Wno-unused -g -O0 $CFLAGS $LINK -o tml2tty tml2tty.c

echo ""

