/*****
 * xscreenshot.c
 * Take screenshot, with selectable area.
 * Copyright (C) James Budiono, 2014
 * License: GNU GPL Version 3 or later.
 *****/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xregion.h>
#include <X11/extensions/Xfixes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include "pnglite.h"

#define DEFAULT_SCREENSHOT_PREFIX "xscreenshot"
#define DEFAULT_SCREENSHOT_TAKEN_MSG "Screenshot taken!"
#define MSG_FONTNAME "dejavu sans"
#define MSG_FONTSIZE "30"
#define MSG_DELAY 3

// so we don't pass thousands of parameters each time
typedef struct
{
	Display *display;
	Window	root_window;
	int     screen;
	int     width, height;
	unsigned int hotkey_keycode, hotkey_keycode2;
	
	GC     pen;	
	Cursor grab_cursor;
	BOX    anchor; //x1,y1=anchor, x2,y2=current pointer
	int    select_in_progress;
	int    mouse_grabbed;

	char *screenshot_path;

	int screenshot_delay;
} ProgState;


// This must be freed when done
char *get_save_fname(ProgState *st)
{
	char *fname;
	char ts[64];
	
	time_t now = time(NULL);
	struct tm *now2 = localtime(&now);
	strftime(ts, sizeof(ts), "%Y%m%dT%H%M%S", now2);
	asprintf (&fname, "%s-%s.png", st->screenshot_path, ts);
	return fname;
}

////////////// SCREEN CAPTURE ROUTINE //////////////
// get shift count based on mask, assumes LSB
int get_shift (int mask)
{
	int n=0, test=1;
	while (n < sizeof(mask)*8) {
		if (mask & test) return n;
		n++; test<<=1;
	}
	return 0;
}

// get 24-bit rgb data from XImage, ZPixmap format
// only supports direct/truecolor 15/16/24 bit, assumes LSB
unsigned char *extract_rgb24_from_ximage (XImage *image)
{
	unsigned char *out=NULL;
	int x,y,index;
	int width, height;
	unsigned long pix;
	
	int red_shift, green_shift, blue_shift;
	int red_mask, green_mask, blue_mask;
	double red_factor=1.0, green_factor=1.0, blue_factor=1.0;
	
	width       = image->width;
	height      = image->height;
	red_mask    = image->red_mask;
	green_mask  = image->green_mask;
	blue_mask   = image->blue_mask;
	red_shift   = get_shift (red_mask);
	green_shift = get_shift (green_mask);
	blue_shift  = get_shift (blue_mask);
	
	switch (image->depth) {
		case 15:
		case 16:
			red_factor   = 255.0/(red_mask   >> red_shift);
			green_factor = 255.0/(green_mask >> green_shift);
			blue_factor  = 255.0/(blue_mask  >> blue_shift);		
		case 24:
		case 32:
			break;
		default:
			fprintf (stderr, "Unsupported bit depth: %d.\n", image->depth);
			return NULL;
	}
	
	out = malloc (width * height * 3); // 24-bit RGB (3 bytes per pixel)	
	for (index=0, y=0; y < height; y++)
	{
		for (x=0; x < width; x++)
		{
			pix = XGetPixel (image, x,y);
			out[index++] = ((pix & red_mask)   >> red_shift) * red_factor;
			out[index++] = ((pix & green_mask) >> green_shift) * green_factor;
			out[index++] = ((pix & blue_mask)  >> blue_shift) * blue_factor;
		}
	}
	return out;
}

// overlay the mouse cursor on top of given image.
void paint_mouse_cursor(ProgState *st, BOX rect, unsigned char *image) {
	/* Check if the XFIXES extension is present */
	int event, error;
	if (XFixesQueryExtension(st->display, &event, &error))
	{
		XFixesCursorImage *cur = XFixesGetCursorImage(st->display);
		
		int sx = (cur->x - cur->xhot), sy = (cur->y - cur->yhot);
		int x,y,dx,dy, cx,cy;
		for (y = sy, dy=cur->height, cy=0;  dy>0; dy--, y++, cy++) {
			for (x = sx, dx=cur->width, cx=0; dx> 0; dx--, x++, cx++) {
				
				if (x>=rect.x1 && y>=rect.y1 && x<rect.x2 && y<rect.y2) {
					unsigned long cur_pixel = cur->pixels [ cy*cur->width + cx ];
					int r = (cur_pixel >> 0) & 0xff;
					int g = (cur_pixel >> 8) & 0xff;
					int b = (cur_pixel >> 16) & 0xff;
					int a = (cur_pixel >> 24) & 0xff;
					
					int img_ofs = ((y-rect.y1)*rect.x2 + (x-rect.x1)) * 3; // 24-bit RGB - 3 bytes per pixel
					if (a == 255) {
						image [ img_ofs +0 ] = r;
						image [ img_ofs +1 ] = g;
						image [ img_ofs +2 ] = b;
					} else {
						image [ img_ofs +0 ] = r + (image [ img_ofs +0 ] * (255 - a) + 255 / 2) / 255;
						image [ img_ofs +1 ] = g + (image [ img_ofs +1 ] * (255 - a) + 255 / 2) / 255;
						image [ img_ofs +2 ] = b + (image [ img_ofs +2 ] * (255 - a) + 255 / 2) / 255;
					}
				}
				
			}
		}
	}
}

// take screenshot (if enabled), using pnglite
void take_screenshot (ProgState *st, BOX rect, int with_mouse) // x2,y2 is width, height
{
	XImage *image;
	png_t png;
	unsigned char *buf;
	char *fname=NULL;
	
	// sanity check - if either height or width is zero, abort
	if (!rect.x2 || !rect.y2) return;
	

	// do actual screenshot
	image = XGetImage (
		st->display, st->root_window, rect.x1, rect.y1, rect.x2, rect.y2, 
		AllPlanes, ZPixmap
	);
	buf = extract_rgb24_from_ximage (image);
	if (with_mouse) 
		paint_mouse_cursor (st, rect, buf);
	
	// save it
	if (buf) {
		fname = get_save_fname(st);
		png_open_file_write (&png, fname);
		png_set_data (&png, image->width, image->height, 8, PNG_TRUECOLOR, buf); //8 bits per channel.
		png_close_file (&png);
		free (buf);	
		free (fname);
	}
	XDestroyImage (image);
	fprintf (stderr, "screenshot taken\n");
}
///////////////// END OF SCREEN CAPTURE ROUTINE ///////////////


// enable notification when our hotkey is pressed
char *grab_hotkey (ProgState *st, char *hotkey)
{
	char *hotkey_name = hotkey;
	unsigned int keycode = 0, keycode2 = 0;
	KeySym keysym, keysym2;
	
	keysym = XStringToKeysym (hotkey);
	if (keysym != NoSymbol)
		keycode = XKeysymToKeycode (st->display, keysym);
	else {
		keycode = atoi(hotkey);
		if (keycode) {
			keysym2 = XKeycodeToKeysym(st->display, keycode, 0);
			keycode2 = XKeysymToKeycode (st->display, keysym2);
			hotkey_name = XKeysymToString (keysym2);
		}
	}
		
	if (keycode)
		XGrabKey (st->display, keycode, AnyModifier, st->root_window, 
		          False, GrabModeAsync, GrabModeAsync);
	if (keycode2 && keycode2 != keycode)
		XGrabKey (st->display, keycode2, AnyModifier, st->root_window, 
		          False, GrabModeAsync, GrabModeAsync);	
	if (!keycode && !keycode2) 
	{
		fprintf (stderr, "I don't understand %s, bailing out.\n", hotkey);
		exit(1);
	}
	st->hotkey_keycode = keycode;
	st->hotkey_keycode2 = keycode2;
	return hotkey_name;
}


// grap or ungrab the mouse pointer
int grab_ungrab_mouse (ProgState *st, int grab)
{
	if (grab)
	{
		//fprintf (stderr, "mouse grab\n");
		XGrabPointer (st->display, st->root_window, False, 
					  ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
					  GrabModeAsync, GrabModeAsync, None, st->grab_cursor, CurrentTime);
		
	} else {
		//fprintf (stderr, "mouse release\n");
		XUngrabPointer (st->display, CurrentTime);
	}
	st->mouse_grabbed = grab;	
}


// draw a rectangle, x2,y2 is width, height
draw_rect (ProgState *st, BOX rect)
{
	//fprintf (stderr, "%d, %d, %d, %d\n", box->x1, box->y1, width, height);
	XDrawRectangle (
		st->display, st->root_window, st->pen, rect.x1, rect.y1, rect.x2, rect.y2
	);
}

// convert anchor points to a rect (x,y,width,height)
BOX convert_anchor_to_rect (BOX *anchor)
{
	BOX out;
	out.x1 = anchor->x1;
	if (anchor->x1 > anchor->x2) out.x1 = anchor->x2;
	out.y1 = anchor->y1;
	if (anchor->y1 > anchor->y2) out.y1 = anchor->y2;
	out.x2 = abs (anchor->x1 - anchor->x2);
	out.y2 = abs (anchor->y1 - anchor->y2);
	return out;
}

// convert anchor points to an area (x,y,width,height) 
// inclusive of the anchors themselves
BOX convert_anchor_to_area (BOX *anchor) {
	BOX out = convert_anchor_to_rect (anchor);
	out.x2++;
	out.y2++;
	return out;
}

// start selection
void start_selection (ProgState *st, int x, int y)
{
	st->anchor.x1 = st->anchor.x2 = x;
	st->anchor.y1 = st->anchor.y2 = y;
	st->select_in_progress = 1;
	draw_rect (st, convert_anchor_to_rect (&st->anchor));
}
// end active selection if currently in progress
void end_selection (ProgState *st)
{
	if (st->select_in_progress)
	{
		st->select_in_progress = 0;
		draw_rect (st, convert_anchor_to_rect (&st->anchor));
	}
}
// draw selection box if selection is in progres
void draw_selection (ProgState *st, int x, int y)
{
	if (st->select_in_progress)
	{
		draw_rect (st, convert_anchor_to_rect (&st->anchor));
		st->anchor.x2 = x;
		st->anchor.y2 = y;
		draw_rect (st, convert_anchor_to_rect (&st->anchor));
	}
}

// show notification on screen that capture has been completed
void notify(ProgState *st, char *msg, int x, int y, int delay) {
	int msglen;
	
	if (!msg) return; 
	msglen = strlen(msg);
	
	XDrawString(st->display, st->root_window, st->pen, x, y, msg, msglen);
	XSync(st->display, False);
	sleep (delay);
	XDrawString(st->display, st->root_window, st->pen, x, y, msg, msglen);
	XSync(st->display, False);	
}

// perform delayed screen capture
void delayed_capture(ProgState *st, int with_mouse)
{
	BOX rect = { .x1=0, .y1=0, .x2=st->width, .y2=st->height };

	fprintf (stderr, "screenshot in %d seconds\n", st->screenshot_delay);
	sleep (st->screenshot_delay);
	take_screenshot (st, rect, with_mouse);
	fprintf (stderr, "screenshot taken\n");
	notify (st, "screenshot taken\n", 0, atoi(MSG_FONTSIZE), MSG_DELAY);
}

// get the area of window under cursor
BOX get_window_area_under_cursor(ProgState *st) 
{
	BOX area;
	area.x1 = area.x2 = area.y1 = area.y2 = 0;
	
	Window root_return, child_return;
	int root_x_return, root_y_return;
	int win_x_return, win_y_return;
	unsigned int mask_return;	

	if (XQueryPointer(st->display, st->root_window, 
			&root_return, &child_return, // we only want this
			&root_x_return, &root_y_return, &win_x_return, &win_y_return, &mask_return) == True)
	{
		Window win = child_return;
		if (win == None) {
			win = root_return;
		} else {
			// raise it to make sure it is not obscured
			XRaiseWindow(st->display,win);
			XFlush(st->display);
			sleep(1); // sleep 1 to ensure display is updated
		}

        int x_return, y_return;
        unsigned int width_return, height_return;
        unsigned int border_width_return;
        unsigned int depth_return;
        
		if (XGetGeometry(st->display, win, &root_return, 
				&x_return, &y_return, &width_return, &height_return, // we only want this
				&border_width_return, &depth_return) == True)
		{
			// handle partial off-screen window - Jake
			if (x_return < 0) { width_return += x_return; x_return = 0; }
			if (y_return < 0) { height_return += y_return; y_return = 0; }
			if ( (x_return + width_return) >= st->width ) { width_return = st->width - x_return; }
			if ( (y_return + height_return) >= st->height ) { height_return = st->height - y_return; }			
			
			area.x1 = x_return; area.y1 = y_return;
			area.x2 = width_return; area.y2 = height_return;
		}
	}
	return area;
}

// main event loop
int event_loop (ProgState *st)
{
	XEvent ev;
	int running=1;
	int state;
	BOX capture_area;
	
	while (running)
	{
next_event:
		XNextEvent(st->display, &ev);
		switch(ev.type)
		{
			case ButtonPress:
				if (ev.xbutton.button == Button1)
					start_selection (st, ev.xbutton.x, ev.xbutton.y);
				break;
				
			case ButtonRelease:
				switch (ev.xbutton.button)
				{
					case Button1:
						if (st->select_in_progress)
						{	// selection done
							end_selection (st);

							capture_area = convert_anchor_to_area (&st->anchor);
							if (capture_area.x2 <= 2 && capture_area.y2 <= 2)
								capture_area = get_window_area_under_cursor (st);

							take_screenshot (st, capture_area, 0);
							if (!st->hotkey_keycode) {
								grab_ungrab_mouse (st, 0);
								running=0;
							}
						}
						break;
						
					case Button3:
						if (st->select_in_progress)
						{
							end_selection (st);
						}
						else
						{	// leave if not running in multi shots mode
							if (!st->hotkey_keycode)
							{
								grab_ungrab_mouse (st, 0);
								running=0;
							}
						}
						
						break;
				}
				break;
				
			case MotionNotify:
				draw_selection (st, ev.xmotion.x, ev.xmotion.y);
				break;
			
			case KeyPress:
				if (ev.xkey.keycode == st->hotkey_keycode ||
				    ev.xkey.keycode == st->hotkey_keycode2 )
				{
					state=0;
					if (ev.xkey.state & ShiftMask) state ^= 1;
					if (ev.xkey.state & ControlMask) state ^= 2;
					if (ev.xkey.state & Mod1Mask) state ^= 4; // Alt
					switch (state)
					{
						case 0: // bare hotkey - enable/disable
							if (st->mouse_grabbed)
							{
								if (st->select_in_progress) 
									end_selection (st);
								grab_ungrab_mouse (st, 0);
							}
							else
							{
								grab_ungrab_mouse (st, 1);
							}
							break;
							
						case 1: // shift - delayed fullscreen shot with mouse
							delayed_capture (st, 1);
							break;
							
						case 2: // ctrl - delayed fullscreen shot without mouse
							delayed_capture (st, 0);
							break;							
							
						case 3: // ctrl-shift - exit
							if (st->select_in_progress) 
								end_selection (st);
							if (st->mouse_grabbed)
								grab_ungrab_mouse (st, 0);
							running=0;
							break;
					}
				}
		}
	}
	return 0;
}


// ***************** Entry point *************
int main (int argc, char*argv[])
{
	ProgState st;
	char *hotkey = NULL;
	XGCValues values;
	unsigned long gcmask;
	XFontStruct *font;
	int opt;

	// default values
	st.hotkey_keycode = st.hotkey_keycode2 = 0;
	asprintf (&st.screenshot_path, "%s/" DEFAULT_SCREENSHOT_PREFIX, getenv("HOME"));
	st.screenshot_delay = 5; // default delay is 5 seconds
	
	// argument processing	
	while ((opt = getopt(argc, argv, "n:p:k:d:h")) != -1) 
	{
		switch (opt) 
		{
			case 'k':
				if (optarg) hotkey = optarg;
				break;
			case 'p':
				if (optarg) st.screenshot_path = optarg;
				break;
			case 'd':
				st.screenshot_delay = atoi (optarg);
				break;
			case 'h':
				fprintf (stderr,
"Usage: xscreenshot [-k hotkey] [-p path] [-n index]\n"
"Copyright (C) James Budiono, 2014. License: GPL version 3.0 or later\n\n"
" -h      : this text\n"
" -k key  : multiple shots mode, use 'key' as hotkey (default none)\n"
" -p path : path/filename for screenshot (default is $HOME/" DEFAULT_SCREENSHOT_PREFIX ")\n"
" -d delay: the delay (in seconds) for fullscreen screenshot (default 5)\n\n"
"If you don't use -k, the program will take one screenshot and exit.\n"
"If you do: press hotkey to enter screenshot taking mode, take as many\n"
"  screenshots, and press hotkey to return back to normal mode.\n"
"  Press Ctrl-Shift-hotkey to terminate program\n"
);
				return 0;
		}
	}

	fprintf (stderr,
"--- Mouse usage ---\n"
"Left-click  - press to start, move to select, release to take shot\n"
"Right-click - cancel current selection, (in single-shot: once more to exit.)\n");

	// initialisations
	png_init (0,0);
    st.display = XOpenDisplay(NULL);
    if (st.display == NULL) {
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }
    st.screen      = DefaultScreen (st.display);
    st.root_window = DefaultRootWindow (st.display);
    st.width       = DisplayWidth(st.display, st.screen);
    st.height      = DisplayHeight(st.display, st.screen);
    st.grab_cursor = XCreateFontCursor (st.display, XC_crosshair);

	values.function = GXinvert;
	values.subwindow_mode = IncludeInferiors;
	values.line_width = 1;
	gcmask = GCFunction | GCSubwindowMode | GCLineWidth;
	font = XLoadQueryFont(st.display, "-*-" MSG_FONTNAME "-*-*-*--" MSG_FONTSIZE "-*-*-*-*-*-*-*");
	if (font)
	{
		values.font = font->fid;
		gcmask |= GCFont;
	}
    st.pen = XCreateGC (st.display, st.root_window, gcmask, &values);
    
    if (hotkey) {
		char *hotkey_name = grab_hotkey (&st, hotkey);
		fprintf (stderr,
"--- Multiple shots mode enabled ---\n"
"%s            - to toggle screenshot taking mode\n"
"Shift-%s      - fullscreen delayed screenshot with mouse\n"
"Ctrl-%s       - fullscreen delayed screenshot without mouse\n"
"Ctrl-Shift-%s - exit\n", hotkey_name, hotkey_name, hotkey_name, hotkey_name);
	}
    else grab_ungrab_mouse (&st, 1);
    fprintf(stderr, "---\n");

	// run
	st.select_in_progress = 0;
    event_loop (&st);
    
    // cleanup
    XFreeGC (st.display, st.pen);
	XFlush (st.display);
	XCloseDisplay (st.display);
	return 0;
}

