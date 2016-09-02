#ifndef LEANSDR_GUI_H
#define LEANSDR_GUI_H

#include <sys/time.h>

#include "framework.h"

namespace leansdr {

  //////////////////////////////////////////////////////////////////////
  // GUI blocks
  //////////////////////////////////////////////////////////////////////
  
#ifdef GUI
  
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
  
  static const int DEFAULT_GUI_DECIMATION = 64;
  
  struct gfx {
    Display *display;
    int screen;
    int w, h;
    Window window;
    GC gc;
    Pixmap dbuf;
    gfx(scheduler *sch, const char *name) {
      window_placement *wp;
      for ( wp=sch->windows; wp->name; ++wp )
	if ( ! strcmp(wp->name, name) ) break;
      if ( wp->name )
	init(wp->name, wp->x, wp->y, wp->w, wp->h);
      else {
	fprintf(stderr, "No placement hints for window '%s'\n", name);
	init(name, -1, -1, 320, 240);
      }
    }
    gfx(const char *name, int _x, int _y, int _w, int _h) {
      init(name, _x, _y, _w, _h);
    }
    void init(const char *name, int _x, int _y, int _w, int _h) {
      buttons = 0;
      clicks = 0;
      mmoved = false;
      w = _w;
      h = _h;
      display = XOpenDisplay(getenv("DISPLAY"));
      if ( ! display ) fatal("display");
      screen = DefaultScreen(display);
      XSetWindowAttributes xswa;
      xswa.event_mask = (ExposureMask|
			 StructureNotifyMask|
			 ButtonPressMask|
			 ButtonReleaseMask|
			 KeyPressMask|
			 KeyReleaseMask|
			 PointerMotionMask);
      xswa.background_pixel = BlackPixel(display, screen);
      window = XCreateWindow(display, DefaultRootWindow(display), 
			     100,100, w,h, 10, CopyFromParent,InputOutput,
			     CopyFromParent, CWEventMask|CWBackPixel,
			     &xswa);
      if ( !window ) fatal("window");
      XStoreName(display, window, name);
      XMapWindow(display, window);
      if ( _x>=0 && _y>=0 )
	XMoveWindow(display, window, _x, _y);
      dbuf = XCreatePixmap(display, window, w, h, DefaultDepth(display,screen));
      gc = XCreateGC(display, dbuf, 0, NULL);
      if ( ! gc ) fatal("gc");
    }
    void clear() {
      setfg(0, 0, 0);
      XFillRectangle(display, dbuf, gc, 0, 0, w, h);
    }
    void show() {
      XCopyArea(display, dbuf, window, gc, 0, 0, w, h, 0, 0);
    }
    void sync() {
      XSync(display, False);
    }
    void events() {
      XEvent ev;
      while ( XCheckWindowEvent(display, window, -1, &ev) ) {
	switch ( ev.type ) {
	case ButtonPress: {
	  int b = ev.xbutton.button;
	  buttons |= 1<<b;
	  clicks |= 1<<b;
	  mx = ev.xbutton.x;
	  my = ev.xbutton.y;
	  break;
	}
	case ButtonRelease: {
	  int b = ev.xbutton.button;
	  buttons &= ~(1<<b);
	  mx = ev.xbutton.x;
	  my = ev.xbutton.y;
	  break;
	}
	case MotionNotify:
	  mx = ev.xbutton.x;
	  my = ev.xbutton.y;
	  mmoved = true;
	  break;
	}
      }
    }
    void setfg(unsigned char r, unsigned char g, unsigned char b) {
      XColor c;
      c.red = r<<8; c.green = g<<8; c.blue = b<<8;
      c.flags = DoRed | DoGreen | DoBlue;
      if ( ! XAllocColor(display, DefaultColormap(display,screen), &c) )
	fatal("color");
      XSetForeground(display, gc, c.pixel);
    }
    void point(int x, int y) {
      XDrawPoint(display, dbuf, gc, x, y);
    }
    void line(int x0, int y0, int x1, int y1) {
      XDrawLine(display, dbuf, gc, x0,y0, x1,y1);
    }
    void text(int x, int y, const char *s) {
      XDrawString(display, dbuf, gc, x,y, s, strlen(s));
    }
    void transient_text(int x, int y, const char *s) {
      XDrawString(display, window, gc, x,y, s, strlen(s));
    }
    int buttons;  // Mask of button states (2|4|8)
    int clicks;   // Same, accumulated (must be cleared by owner)
    int mx, my;   // Cursor position
    bool mmoved;  // Pointer moved (must be cleared by owner)
  };
  
  template<typename T>
  struct cscope : runnable {
    T xymin, xymax;
    unsigned long decimation;
    unsigned long pixels_per_frame;
    cscope(scheduler *sch, pipebuf< complex<T> > &_in, T _xymin, T _xymax,
	   const char *_name=NULL)
      : runnable(sch, _name?_name:_in.name),
	xymin(_xymin), xymax(_xymax),
	decimation(DEFAULT_GUI_DECIMATION), pixels_per_frame(1024),
	in(_in), phase(0), g(sch, name) {
    }
    void run() {
      while ( in.readable() >= pixels_per_frame ) {
	if ( ! phase ) {
	  draw_begin();
	  g.setfg(0, 255, 0);
	  complex<T> *p = in.rd(), *pend = p+pixels_per_frame;
	  for ( ; p<pend; ++p )
	    g.point(g.w*(p->re-xymin)/(xymax-xymin),
		    g.h - g.h*(p->im-xymin)/(xymax-xymin));
	  g.show();
	  g.sync();
	}
	in.read(pixels_per_frame);
	if ( ++phase >= decimation ) phase = 0;
      }
    }
    //private:
    pipereader< complex<T> > in;
    unsigned long phase;
    gfx g;
    void draw_begin() {
      g.clear();
      g.setfg(0, 255, 0);
      g.line(g.w/2,0, g.w/2, g.h);
      g.line(0,g.h/2, g.w,g.h/2);
    }	
  };
  
  template<typename T>
  struct wavescope : runnable {
    T ymin, ymax;
    unsigned long decimation;
    wavescope(scheduler *sch, pipebuf<T> &_in,
	      T _ymin, T _ymax, const char *_name=NULL)
      : runnable(sch, _name?_name:_in.name),
	in(_in), ymin(_ymin), ymax(_ymax),
	decimation(DEFAULT_GUI_DECIMATION),
	g(sch, name), phase(0),
	x(0) {
      g.clear();
    }	
    void run() {
      while ( in.readable() >= g.w ) {
	if ( ! phase ) plot(in.rd(), g.w);
	in.read(g.w);
	if ( ++phase >= decimation ) phase = 0;
      }
    }
    void plot(T *p, int count) {
      T *pend = p + count;
      g.clear();
      g.setfg(0, 255, 0);
      for ( int x=0; p<pend; ++x,++p ) {
	T v = *p;
	g.point(x, g.h-1 - (g.h-1)*(v-ymin)/(ymax-ymin));
      }
      g.show();
      g.sync();
    }

  private:
    pipereader<T> in;
    int phase;
    gfx g;
    int x;
  };

  template<typename T>
  struct slowmultiscope : runnable {
    struct chanspec {
      pipebuf<T> *in;
      const char *name, *format;
      unsigned char rgb[3];
      float scale;
      float ymin, ymax;
      enum flag {
	DEFAULT = 0,
	ASYNC = 1,  // Read whatever is available
	COUNT = 2,  // Display number of items read instead of value
	SUM   = 4,  // Display sum of values
	LINE  = 8,  // Connect points
      } flags;
    };
    unsigned long samples_per_pixel;
    float sample_freq;  // Sample rate in Hz (used for cursor operations)
    slowmultiscope(scheduler *sch, const chanspec *specs, int _nchans,
		   const char *_name)
      : runnable(sch, _name?_name:"slowmultiscope"),
	samples_per_pixel(1), sample_freq(1),
	nchans(_nchans),
	g(sch, name), t(0), x(0), total_samples(0) {
      chans = new channel[nchans];
      for ( int i=0; i<nchans; ++i ) {
	chans[i].spec = specs[i];
	chans[i].in = new pipereader<T>(*specs[i].in);
	chans[i].accum = 0;
      }
      g.clear();
    }
    void run() {
      // Read up to one pixel worth of data
      unsigned long count = samples_per_pixel;
      for ( channel *c=chans; c<chans+nchans; ++c )
	if ( ! (c->spec.flags&chanspec::ASYNC) )
	  count = min(count, c->in->readable());
      for ( int n=count; n--; ) {
	for ( channel *c=chans; c<chans+nchans; ++c ) {
	  int nr;
	  if ( c->spec.flags & chanspec::ASYNC )
	    // For async channels, read any and all available data.
	    nr = c->in->readable();
	  else
	    nr = 1;
	  g.setfg(c->spec.rgb[0], c->spec.rgb[1], c->spec.rgb[2]);
	  int y = -1;
	  while ( nr-- ) {
	    float v = *c->in->rd() * c->spec.scale;
	    if ( c->spec.flags & chanspec::COUNT )
	      ++c->accum;
	    else if ( c->spec.flags & chanspec::SUM )
	      c->accum += v;
	    else {
	      c->print_val = v;
	      y = g.h - g.h*(v-c->spec.ymin)/(c->spec.ymax-c->spec.ymin);
	    }
	    c->in->read(1);
	  }
	  // Display count/sum channels only when the cursor is about to move.
	  if ( (c->spec.flags&(chanspec::COUNT|chanspec::SUM)) &&
	       t+1 >= samples_per_pixel ) {
	    T v = c->accum;
	    y = g.h-1 - g.h*(v-c->spec.ymin)/(c->spec.ymax-c->spec.ymin);
	    c->accum = 0;
	    c->print_val = v;
	  } 
	  if ( y >= 0 ) {
	    if ( c->spec.flags & chanspec::LINE ) {
	      if ( x ) g.line(x-1, c->prev_y, x, y);
	      c->prev_y = y;
	    } else
	      g.point(x, y);
	  }
	}
	g.show();
	// Print instantatenous values as text
	for ( int i=0; i<nchans; ++i ) {
	  channel *c = &chans[i];
	  g.setfg(c->spec.rgb[0], c->spec.rgb[1], c->spec.rgb[2]);
	  char text[256];
	  sprintf(text, c->spec.format, c->print_val);
	  g.transient_text(5, 20+16*i, text);
	}
	run_gui();
	if ( ++t >= samples_per_pixel ) {
	  t = 0;
	  ++x;
	  if ( x >= g.w ) x = 0;
	  g.setfg(0, 0, 0);
	  g.line(x, 0, x, g.h-1);
	}
	run_gui();
	g.sync();
      }
      total_samples += count;
    }
    void run_gui() {
      g.events();
      // Print cursor time
      float ct = g.mx * samples_per_pixel / sample_freq;
      float tt = total_samples / sample_freq;
      char text[256];
      sprintf(text, "%.3f / %.3f s", ct, tt);
      g.setfg(255, 255, 255);
      g.transient_text(g.w*3/4, 20, text);
    }
  private:
    int nchans;
    struct channel {
      chanspec spec;
      pipereader<T> *in;
      float accum;
      int prev_y;
      float print_val;
    } *chans;
    gfx g;
    unsigned long t;
    int x;
    int total_samples;
  };
  
  template<typename T>
  struct spectrumscope : runnable {
    T ymax;
    float amax;
    unsigned long size;
    unsigned long decimation;
    spectrumscope(scheduler *sch, pipebuf< complex<T> > & _in,
		  T _max, const char *_name=NULL)
      : runnable(sch, _name?_name:_in.name),
	ymax(_max), amax(_max),
	size(4096), decimation(DEFAULT_GUI_DECIMATION),
	in(_in), phase(0), g(sch, name), fft(NULL) {
    }
    void run() {
      while ( in.readable() >= size ) {
	if ( ! phase ) do_fft(in.rd());
	in.read(size);
	if ( ++phase >= decimation ) phase = 0;
      }
    }
  private:
    pipereader< complex<T> > in;
    int phase;
    gfx g;
    cfft_engine<float> *fft;
    void do_fft(complex<T> *input) {
      draw_begin();
      if ( !fft || fft->n!=size ) {
	if ( fft ) delete fft;
	fft = new cfft_engine<float>(size);
      }
      complex<T> *pin=input, *pend=pin+size;
      complex<float> data[size], *pout=data;
      g.setfg(255, 0, 0);
      for ( int x=0; pin<pend; ++pin,++pout,++x ) {
	pout->re = (float)pin->re;
	pout->im = (float)pin->im;
	// g.point(x, g.h/2-pout->re*g.h/2/ymax);
      }
      fft->inplace(data, true);
      g.setfg(0, 255, 0);
      for ( int i=0; i<size; ++i ) {
	int x = ((i<size/2)?i+size/2:i-size/2) * g.w / size;
	complex<float> v = data[i];;
	float y = hypot(v.re, v.im);
	g.line(x, g.h-1, x, g.h-1-y*g.h/amax);
      }
      if ( g.buttons ) {
	char s[256];
	float f = 2.4e6 * (g.mx-g.w/2) / g.w;
	sprintf(s, "%f", f);
	g.text(16, 16, s);
      }
      g.show();
      g.sync();
    }
    void draw_begin() {
      g.clear();
      g.setfg(255, 255, 255);
      g.line(g.w/2,0, g.w/2,g.h);
    }
  };
  
  template<typename T>
  struct genscope : runnable {
    struct render {
      int x, y;
      char dir;  // 'h'orizontal or 'v'ertical
    };
    struct chanspec {
      pipebuf<T> *in;  // NULL if disabled
      render r;
    };
    genscope(scheduler *sch, chanspec *specs, int _nchans,
	     const char *_name=NULL)
      : runnable(sch, _name?_name:"genscope"),
	nchans(_nchans),
	g(sch, name) {
      chans = new channel[nchans];
      for ( int i=0; i<nchans; ++i ) {
	if ( ! specs[i].in ) {
	  chans[i].in = NULL;
	} else {
	  chans[i].spec = specs[i];
	  chans[i].in = new pipereader<T>(*specs[i].in);
	}
      }
      g.clear();
      gettimeofday(&tv, NULL);
    }
    struct channel {
      chanspec spec;
      pipereader<T> *in;
    } *chans;
    int nchans;
    struct timeval tv;
    void run() {
      g.setfg(0, 255, 0);
      for ( channel *pc=chans; pc<chans+nchans; ++pc ) {
	if ( ! pc->in ) continue;
	int n = pc->in->readable();
	T last = pc->in->rd()[n-1];
	pc->in->read(n);
	int dx = pc->spec.r.dir=='h' ? last : 0;
	int dy = pc->spec.r.dir=='v' ? last : 0;
	dx /= 3;
	dy /= 3;
	g.line(pc->spec.r.x-dx, pc->spec.r.y-dy,
	       pc->spec.r.x+dx, pc->spec.r.y+dy);
	char txt[16];
	sprintf(txt, "%d", (int)last);
	g.text(pc->spec.r.x+5, pc->spec.r.y-2, txt);
      }
      struct timeval newtv;
      gettimeofday(&newtv, NULL);
      int dt = (newtv.tv_sec-tv.tv_sec)*1000 + (newtv.tv_usec-tv.tv_usec)/1000;
      if ( dt > 100 ) {
	fprintf(stderr, "#");
	g.show();
	g.sync();
	g.clear();
	tv = newtv;
      }
    }
  private:
    gfx g;
  };

#endif  // GUI

}  // namespace

#endif  // LEANSDR_GUI_H
