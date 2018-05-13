/*****
 * xscreenshot.c
 * Take screenshot, with selectable area.
 * Copyright (C) James Budiono, 2014
 * License: GNU GPL Version 3 or later.
 *****/

/*
 * Modifications by Raymond Nicholson, 2018
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xregion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>


#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

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

////////////// SCREEN CAPTURE ROUTINE //////////////

// take screenshot (if enabled)
void take_screenshot (ProgState *st, BOX rect, int with_mouse) // x2,y2 is width, height
{
	XImage *img;
	uint32_t tmp, w, h;
	uint16_t rgba[4];
	int sr, sg, fr, fg, fb;
	
	// sanity check - if either height or width is zero, abort
	if (!rect.x2 || !rect.y2) return;
	
	// do actual screenshot
	img = XGetImage (
		st->display, st->root_window, rect.x1, rect.y1, rect.x2, rect.y2, 
		AllPlanes, ZPixmap
	);

	switch (img->bits_per_pixel) {
	case 16: /* only 5-6-5 format supported */
		sr = 11;
		sg = 5;
		fr = fb = 2047;
		fg = 1023;
		break;
	case 24:
	case 32: /* ignore alpha in case of 32-bit */
		sr = 16;
		sg = 8;
		fr = fg = fb = 257;
		break;
	default:
		errx(1, "unsupported bpp: %d", img->bits_per_pixel);
	}

	/* write header with big endian width and height-values */
	fprintf(stdout, "farbfeld");
	tmp = htonl(img->width);
	fwrite(&tmp, sizeof(uint32_t), 1, stdout);
	tmp = htonl(img->height);
	fwrite(&tmp, sizeof(uint32_t), 1, stdout);

	/* write pixels */
	for (h = 0; h < (uint32_t)img->height; h++) {
		for (w = 0; w < (uint32_t)img->width; w++) {
			tmp = XGetPixel(img, w, h);
			rgba[0] = htons(((tmp & img->red_mask) >> sr) * fr);
			rgba[1] = htons(((tmp & img->green_mask) >> sg) * fg);
			rgba[2] = htons((tmp & img->blue_mask) * fb);
			rgba[3] = htons(65535);

			if (fwrite(&rgba, 4 * sizeof(uint16_t), 1, stdout) != 1)
				err(1, "fwrite");
		}
	}

	// TODO emit screenshit
	
	XDestroyImage (img);
}
///////////////// END OF SCREEN CAPTURE ROUTINE ///////////////

// grap or ungrab the mouse pointer
void grab_ungrab_mouse (ProgState *st, int grab)
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
void draw_rect (ProgState *st, BOX rect)
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

int main (int argc, char *argv[]) {
	ProgState st;
	XGCValues values;

	st.display = XOpenDisplay(NULL);
	if (!st.display) {
		return 1;
	}
	st.screen      = DefaultScreen (st.display);
	st.root_window = DefaultRootWindow (st.display);
	st.width       = DisplayWidth(st.display, st.screen);
	st.height      = DisplayHeight(st.display, st.screen);
	st.grab_cursor = XCreateFontCursor (st.display, XC_crosshair);

	values.function = GXinvert;
	values.subwindow_mode = IncludeInferiors;
	values.line_width = 1;
	st.pen = XCreateGC (st.display, st.root_window, GCFunction | GCSubwindowMode | GCLineWidth, &values);
	
	grab_ungrab_mouse (&st, 1);
	st.select_in_progress = 0;
	event_loop (&st);

	XFreeGC (st.display, st.pen);
	XFlush (st.display);
	XCloseDisplay (st.display);
	
	return 0;
}

