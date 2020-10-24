#!/bin/sh

set -e

CFLAGS=
LINK=

if [ -z "${CC}" ]; then
  CC=gcc
fi

# get us some color
normal="$(tput sgr0)"
red="$(tput setaf 1)"
green="$(tput setaf 2)"

say() {
  echo "$@""${normal}"
}

fail() {
  say -n "${red}Error"
  say ": $*"
  exit 1
}

verbose() {
  if "$@"; then
    say "[${green}okay${normal}] $*"
  else
    local rc=$?
    say "[${red}FAIL${normal}] [rc=${rc}] $*"
  fi
}

pkg() {
  say -n "Looking for $1: "
  if pkg-config --exists $1; then
    say "${green}$(pkg-config --modversion $1)"
    CFLAGS="$CFLAGS $(pkg-config --cflags $1)"
    LINK="$LINK $(pkg-config --libs $1)"
    return 0
  else
    say "${red}not found"
    return 1
  fi
}

if ! pkg expat; then
  fail "Need expat installed."
fi

if pkg tinfo; then
  true
if pkg ncursesw; then
  true
elif pkg ncurses; then
  true
else
  echo "Need ncursesw or ncurses installed."
  exit 1
fi

echo ""

verbose ${CC} -Wall -Wextra -Wno-unused -Wno-unused-parameter -g -O0 $CFLAGS $LINK -o tml2tty tml2tty.c

echo ""

