## TML - Terminal markup language

This is a little tool I wrote that can format a crooked subset of Pango style annotations to ANSI strings using terminfo.

Use like this:
    user@dev-general:~/tml$ ./compile.sh
    user@dev-general:~/tml$ ./tml2tty -h
    Usage: ./tml2tty [<higher magic>]
    user@dev-general:~/tml$ ./tml2tty "<red>I</red> am feeling <i>italic</i>."
    I am feeling italic.
    user@dev-general:~/tml$ cat demo.xml | ./tml2tty 
    Test Document
    This is a test document. It's purpose is to test the TML to TTY converter.
    TESTTESTTEST
    user@dev-general:~/tml$ 

Output should colored on terminals that support it.

