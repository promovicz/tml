## TML - Terminal markup language

This is a little tool I wrote that can format a crooked subset of Pango style annotations to ANSI strings using terminfo.

Currently this is a single-pass utility, and there are no format instructions except for line breaks.

Uses expat for XML parsing and ncurses/terminfo for terminal support.

Use like this:
```
user@host:~/tml$ ./compile.sh
user@host:~/tml$ ./tml2tty -h
Usage: ./tml2tty [<higher magic>]
user@host:~/tml$ ./tml2tty "<red>I</red> am feeling <i>italic</i>."
I am feeling italic.
user@host:~/tml$ cat demo.xml | ./tml2tty 
Test Document
This is a test document. It's purpose is to test the TML to TTY converter.
TESTTESTTEST
user@host:~/tml$ 
```

Output should be colored on terminals that support it.

Also see asciicast: https://asciinema.org/a/367208.

