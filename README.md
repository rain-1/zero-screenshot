This is a screenshot tool for linux/X11.

I built it by stripping down xannotate's xscreenshot, replacing part of it with farbfeld xscreenshot.

To use it put `ff2png` (from farbfeld) and `screenshot-bin` (built using `makefile.sh`) in `$PATH`. Then run ./screenshot. There is a desktop file too so you can put it in your XFCE menu or whatever.

* http://lightofdawn.org/wiki/wiki.cgi/Xannotate
* https://tools.suckless.org/farbfeld/
