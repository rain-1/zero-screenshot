This is a screenshot tool for linux/X11.

I built it by stripping down xannotate's xscreenshot, replacing part of it with farbfeld xscreenshot.

To use it put `ff2png` (from farbfeld) and `screenshot-bin` (built using `makefile.sh`) in `$PATH`. Then run ./screenshot.

There is a desktop file too which you can drag to your XFCE bar or `mkdir -p ~/.local/share/applications/ ; cp Screenshot.desktop ~/.local/share/applications/`.

* http://lightofdawn.org/wiki/wiki.cgi/Xannotate
* https://tools.suckless.org/farbfeld/
