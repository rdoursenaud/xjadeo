/* 
   xjadeo - OSC remote control

   Copyright (C) 2007,2009 Robin Gareus

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 
   as published by the Free Software Foundation;

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
*/


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef HAVE_LIBLO

#include <stdio.h>
#include <unistd.h>
#include <lo/lo_lowlevel.h>
#include "xjadeo.h"

extern int	want_verbose;
extern int	want_quiet;
extern int	loop_flag;
extern int	loop_run;
extern int 	force_redraw;
extern int 	movie_width;
extern long	userFrame;
extern long	dispFrame;
extern double 	delay;
extern double 	filefps;
extern long	ts_offset;

#ifdef HAVE_MIDI
extern int midi_clkconvert;
extern int midi_clkadj;
extern char midiid[32];
#endif

#ifdef TIMEMAP
extern long   timeoffset;
extern double timescale;
extern int    wraparound;
#endif

#ifdef CROPIMG
  extern int xoffset;
#endif

extern double	delay;
extern double	framerate;

int oscb_seek (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- i:%i\n", path, argv[0]->i);
  userFrame=argv[0]->i;
  return(0);
}

int oscb_fps (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- f:%f\n", path, argv[0]->f);
  if (argv[0]->f>0) delay= 1.0 / argv[0]->f;
  else delay = -1; // use file-framerate
  return(0);
}

int oscb_framerate (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- f:%f\n", path, argv[0]->f);
  filefps=argv[0]->f;
  override_fps(filefps);
  return(0);
}

int oscb_offset (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- i:%i\n", path, argv[0]->i);
  ts_offset = argv[0]->i;
  return(0);
}

int oscb_offsetsmpte (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- s:%s\n", path, &argv[0]->s);
  ts_offset = smptestring_to_frame((char*)&argv[0]->s);
  return(0);
}

int oscb_jackconnect (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  open_jack();
  return(0);
}

int oscb_jackdisconnect (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  close_jack();
  return(0);
}

#ifdef HAVE_MIDI
int oscb_midiconnect (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  char *mp;
  if (&argv[0]->s && strlen(&argv[0]->s)>0) mp=&argv[0]->s; 
  else mp="-1"; 
  midi_open(mp);
  if (midi_connected()) {
    strncpy(midiid,mp,32);
    midiid[31]=0;
  }
  return(0);
}

int oscb_mididisconnect (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  midi_close();
  return(0);
}

int oscb_midiquarterframes (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- i:%i\n", path, argv[0]->i);
  midi_clkadj = argv[0]->i?1:0;
  return(0);
}

int oscb_midiclkconvert (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- i:%i\n", path, argv[0]->i);
  midi_clkconvert = argv[0]->i;
  return(0);
}
#endif

#ifdef TIMEMAP
int oscb_timescale (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- f:%f\n", path, argv[0]->f);
  timescale=argv[0]->f;
  force_redraw=1;
  return(0);
}

int oscb_timescale2 (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- f:%f i:%i\n", path, argv[0]->f, argv[1]->i);
  timescale=argv[0]->f;
  timeoffset=argv[1]->i;
  force_redraw=1;
  return(0);
}

int oscb_loop (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- i:%i\n", path, argv[0]->i);
  wraparound = argv[0]->i?1:0;
  return(0);
}

int oscb_reverse (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s\n", path);
  timescale *= -1.0; 
  if (timescale<0) 
    timeoffset = (-2.0*timescale) * dispFrame; // TODO: check file-offset and ts_offset. -> also in remote.c
  else 
    timeoffset = 0; // TODO - applt diff dispFrame <> transport src
  return(0);
}
#endif

#ifdef CROPIMG
int oscb_pan (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- i:%i\n", path, argv[0]->i);
  xoffset=argv[0]->i;
  if (xoffset<0) xoffset=0;
  if (xoffset>movie_width) xoffset=movie_width; 
  force_redraw=1;
  return(0);
}
#endif

int oscb_load (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (1 || want_verbose) fprintf(stderr, "OSC: %s <- s:%s\n", path, &argv[0]->s);
  open_movie(&argv[0]->s);
  init_moviebuffer();
  newsourcebuffer();
  force_redraw=1;
  return(0);
}

// OSD
extern char OSD_fontfile[1024]; 
extern int OSD_mode;

int oscb_osdfont (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (1 || want_verbose) fprintf(stderr, "OSC: %s <- s:%s\n", path, &argv[0]->s);
  snprintf(OSD_fontfile,1024,"%s",(char*) &argv[0]->s);
  return(0);
}

int oscb_osdframe (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- i:%i\n", path, argv[0]->i);
  if (argv[0]->i) 
    OSD_mode|=OSD_FRAME;
  else
    OSD_mode&=~OSD_FRAME;
  force_redraw=1;
  return(0);
}

int oscb_osdsmtpe (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- i:%i\n", path, argv[0]->i);
  if (argv[0]->i) 
    OSD_mode|=OSD_SMPTE;
  else
    OSD_mode&=~OSD_SMPTE;
  force_redraw=1;
  return(0);
}

int oscb_osdbox (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- i:%i\n", path, argv[0]->i);
  if (argv[0]->i) 
    OSD_mode|=OSD_BOX;
  else
    OSD_mode&=~OSD_BOX;
  force_redraw=1;
  return(0);
}

int oscb_remotecmd (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- s:%s\n", path, &argv[0]->s);
  exec_remote_cmd (&argv[0]->s);
  return(0);
}

// X11 options

/*
int oscb_fullscreen (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- i:%i\n", path, argv[0]->i);
  int action=_NET_WM_STATE_TOGGLE;
  if (!strcmp(d,"on") || atoi(d)==1) action=_NET_WM_STATE_ADD;
  else if (!strcmp(d,"off")) action=_NET_WM_STATE_REMOVE;
  remote_printf(100,"ok.");
  Xfullscreen(action);
  return(0);
}

int oscb_mousepointer (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  if (want_verbose) fprintf(stderr, "OSC: %s <- i:%i\n", path, argv[0]->i);
  int action=2;
  if (!strcmp(d,"on") || atoi(d)==1) action=1;
  else if (!strcmp(d,"off")) action=0;
  Xmousepointer (action);
  remote_printf(100,"ok.");
  return(0);
}
*/

// general

int oscb_quit (const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data){
  fprintf(stderr, "OSC 'quit' command recv.\n");
  loop_flag=0;
  return(0);
}

static void oscb_error(int num, const char *m, const char *path) {
  fprintf(stderr, "liblo server error %d in path %s: %s\n", num, path, m);
}

//////////////////////////////////////////////////////////////////////////////

lo_server osc_server = NULL;

int initialize_osc(int osc_port) {
  char tmp[8];
  uint32_t port = (osc_port>100 && osc_port< 60000)?osc_port:7000;

  snprintf(tmp, sizeof(tmp), "%d", port);
  fprintf(stderr, "OSC trying port:%i\n",port);
  osc_server = lo_server_new (tmp, oscb_error);
//fprintf (stderr,"OSC port %i is in use.\n", port);

  if (!osc_server) {
    if(!want_quiet) fprintf(stderr, "OSC start failed.");
    return(1);
  }

  if(!want_quiet) {
    char *urlstr;
    urlstr = lo_server_get_url (osc_server);
    fprintf(stderr, "OSC server name: %s\n",urlstr);
    free (urlstr);
  }

  lo_server_add_method(osc_server, "/jadeo/seek", "i", &oscb_seek, NULL); // IFF no MIDI-TC and no JACK-TS - seek to this frame
  lo_server_add_method(osc_server, "/jadeo/load", "S", &oscb_load, NULL);
#ifdef CROPIMG
  lo_server_add_method(osc_server, "/jadeo/pan", "i", &oscb_pan, NULL);
#endif
#ifdef TIMEMAP
  lo_server_add_method(osc_server, "/jadeo/timescale", "f", &oscb_timescale, NULL);
  lo_server_add_method(osc_server, "/jadeo/timescale", "fi", &oscb_timescale2, NULL);
  lo_server_add_method(osc_server, "/jadeo/loop", "i", &oscb_loop, NULL);
  lo_server_add_method(osc_server, "/jadeo/reverse", "", &oscb_reverse, NULL);
#endif
  lo_server_add_method(osc_server, "/jadeo/fps", "f", &oscb_fps, NULL); // set screen update fps
  lo_server_add_method(osc_server, "/jadeo/framerate", "f", &oscb_framerate, NULL); //  override file's fps
  lo_server_add_method(osc_server, "/jadeo/offset", "i", &oscb_offset, NULL); // set offset by frame-number
  lo_server_add_method(osc_server, "/jadeo/offset", "s", &oscb_offsetsmpte, NULL); // set offset as SMPTE

  lo_server_add_method(osc_server, "/jadeo/jack/connect", "", &oscb_jackconnect, NULL);
  lo_server_add_method(osc_server, "/jadeo/jack/disconnect", "", &oscb_jackdisconnect, NULL);
#ifdef HAVE_MIDI
  lo_server_add_method(osc_server, "/jadeo/midi/connect", "s", &oscb_midiconnect, NULL);
  lo_server_add_method(osc_server, "/jadeo/midi/disconnect", "", &oscb_mididisconnect, NULL);
  lo_server_add_method(osc_server, "/jadeo/midi/quarterframes", "i", &oscb_midiquarterframes, NULL);
  lo_server_add_method(osc_server, "/jadeo/midi/clkconvert", "i", &oscb_midiclkconvert, NULL);
#endif

  lo_server_add_method(osc_server, "/jadeo/osd/font", "s", &oscb_osdfont, NULL);
  lo_server_add_method(osc_server, "/jadeo/osd/smtpe", "i", &oscb_osdsmtpe, NULL);
  lo_server_add_method(osc_server, "/jadeo/osd/frame", "i", &oscb_osdframe, NULL);
  lo_server_add_method(osc_server, "/jadeo/osd/box", "i", &oscb_osdbox, NULL);

  lo_server_add_method(osc_server, "/jadeo/cmd", "s", &oscb_remotecmd, NULL);

  lo_server_add_method(osc_server, "/jadeo/quit", "", &oscb_quit, NULL);

  if(want_verbose) fprintf(stderr, "OSC server started on port %i\n",port);
  return (0);
}

int process_osc(void) {
  int rv = 0;
  if (!osc_server) return 0;
  while (lo_server_recv_noblock(osc_server, 0) > 0) {
    rv++;
  }
  return rv;
}

void shutdown_osc(void) {
  if (!osc_server) return;
  lo_server_free(osc_server);
  if(!want_verbose) fprintf(stderr, "OSC server shut down.\n");
}

#else
int initialize_osc(int osc_port) {return(1);}
void shutdown_osc(void) {;}
int process_osc(void) {return(0);}
#endif

/* vi:set ts=8 sts=2 sw=2: */
