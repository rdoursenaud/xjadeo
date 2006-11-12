/* xjadeo - jack video monitor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
 *
 * Credits:
 *
 * xjadeo:  (c) 2006 
 *  Luis Garrido <luisgarrido@users.sourceforge.net>
 *  Robin Gareus <robin@gareus.org>
 *
 * XLib code:
 * http://www.ac3.edu.au/SGI_Developer/books/XLib_PG/sgi_html/index.html
 *
 * WM_DELETE_WINDOW code:
 * http://biology.ncsa.uiuc.edu/library/SGI_bookshelves/SGI_Developer/books/OpenGL_Porting/sgi_html/apf.html
 *
 * ffmpeg code:
 * http://www.inb.uni-luebeck.de/~boehme/using_libavcodec.html
 *
 */

#define EXIT_FAILURE 1

#include "xjadeo.h"

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>

#include <getopt.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


//------------------------------------------------
// Globals
//------------------------------------------------

// Display loop

/* int loop_flag: main xjadeo event loop 
 * if loop_flag is set to 0, xjadeo will exit
 */
int       loop_flag = 1; 

/* int loop_run: video update enable
 * if set to 0 no smpte will be polled and 
 * no video frame updates are performed.
 */
int 	  loop_run = 1; 

      
// Video Decoder 

int               movie_width = 100;
int               movie_height = 100;
AVFormatContext   *pFormatCtx;
int               videoStream=-1;
AVCodecContext    *pCodecCtx;
AVFrame           *pFrame;
AVFrame           *pFrameFMT = NULL;
uint8_t           *buffer = NULL;

int               render_fmt = PIX_FMT_YUV420P; ///< needs to be set before calling movie_open

/* Video File Info */
double	duration = 1;
double	framerate = 1;
long	frames = 1;


/* Option flags and variables */
char	*current_file = NULL;
long	ts_offset = 0;
long	userFrame = 0; // seek to this frame if jack and midi are N/A
long	dispFrame = 0; // global strorage... = (SMPTE+offset) with boundaries to [0..movie_file_frames]

int want_quiet   =0;	/* --quiet, --silent */
int want_debug   =0;	/* --debug */
int want_verbose =0;	/* --verbose */
int start_ontop =0;	/* --ontop // -a */
int start_fullscreen =0;/* NY available */
int avoid_lash   =0;	/* --nolash */
int remote_en =0;	/* --remote, -R */
int remote_mode =0;	/* 0: undirectional ; >0: bidir
			 * bitwise enable async-messages 
			 *  so far only: 
			 *   (1) notify timecode 
			 *   (2) notify changed timecode 
			 */

int try_codec =0;	/* --try-codec */

#ifdef HAVE_MIDI
char midiid[32] = "-2";	/* --midi # -1: autodetect -2: jack*/
int midi_clkconvert =0;	/* --midifps [0:MTC|1:VIDEO|2:RESAMPLE] */
#endif

#ifdef HAVE_LASH
lash_client_t *lash_client;
#endif


double 	delay = 0.1; // default update rate 10 Hz
double 	filefps = -1.0; // if > 0 override autodetected video file frame rate
int	videomode = 0; // --vo <int>  - default: autodetect

int 	seekflags    = SEEK_ANY; 


// On screen display
char OSD_fontfile[1024] = FONT_FILE;
char OSD_text[128] = "xjadeo!";
char OSD_frame[48] = "";
char OSD_smpte[13] = "";
int OSD_mode = 0;

int OSD_fx = OSD_CENTER;
int OSD_tx = OSD_CENTER;
int OSD_sx = OSD_CENTER;
int OSD_fy = 5; // percent
int OSD_sy = 98; // percent
int OSD_ty = 50; // percent

/* The name the program was run with, stripped of any leading path. */
char *program_name;


// prototypes .

static void usage (int status);
static void printversion (void);

static struct option const long_options[] =
{
  {"quiet", no_argument, 0, 'q'},
  {"silent", no_argument, 0, 'q'},
  {"verbose", no_argument, 0, 'v'},
  {"keyframes", no_argument, 0, 'k'},
  {"continuous", required_argument, 0, 'K'},
  {"offset", no_argument, 0, 'o'},
  {"fps", required_argument, 0, 'f'},
  {"filefps", required_argument, 0, 'F'},
  {"videomode", required_argument, 0, 'm'},
  {"vo", required_argument, 0, 'x'},
  {"remote", no_argument, 0, 'R'},
  {"help", no_argument, 0, 'h'},
  {"version", no_argument, 0, 'V'},
  {"try-codec", no_argument, 0, 't'},
  {"info", no_argument, 0, 'i'},
  {"ontop", no_argument, 0, 'a'},
  {"nolash", no_argument, 0, 'L'},
#ifdef HAVE_MIDI
  {"midi", required_argument, 0, 'm'},
  {"midifps", required_argument, 0, 'M'},
#endif
  {"debug", no_argument, 0, 'D'},
  {NULL, 0, NULL, 0}
};



/* Set all the option flags according to the switches specified.
   Return the index of the first non-option argument.  */
static int
decode_switches (int argc, char **argv)
{
  int c;
  while ((c = getopt_long (argc, argv, 
			   "q"	/* quiet or silent */
			   "v"	/* verbose */
			   "h"	/* help */
			   "R"	/* remote control */
			   "k"	/* keyframes */
			   "K"	/* anyframe */
			   "o:"	/* offset */
			   "t"	/* try-codec */
			   "f:"	/* fps */
			   "F:"	/* file FPS */
			   "x:"	/* video-mode */
			   "a"	/* always on top */
			   "i:"	/* info - OSD-mode */
#ifdef HAVE_MIDI
			   "m:"	/* midi interface */
			   "M:"	/* midi clk convert */
#endif
			   "D"	/* debug */
			   "L"	/* no lash */
			   "V",	/* version */
			   long_options, (int *) 0)) != EOF)
  {
    switch (c) {
	case 'q':		/* --quiet, --silent */
	  want_quiet = 1;
	  want_verbose = 0;
	  break;
	case 'D':		/* --debug */
	  want_debug = 1;
	  break;
	case 'v':		/* --verbose */
#ifndef HAVE_MQ
	  want_verbose = !remote_en;
#else
	  want_verbose = 1;
#endif
	  break;
	case 'R':		/* --remote */
	  remote_en = 1;
#ifndef HAVE_MQ
	  want_quiet = 1;
	  want_verbose = 0;
#endif
	  break;
	case 'L':		/* --nolash */
	  avoid_lash = 1;
	  break;
	case 't':		/* --try */
	  try_codec = 1;
	  break;
	case 'i':		/* --info */
	  OSD_mode=atoi(optarg)&3;
	  if (!want_quiet) printf("On screen display: [%s%s%s] \n",
		(!OSD_mode)?"off": 
		(OSD_mode&OSD_FRAME)?"frames":"",
		(OSD_mode&(OSD_FRAME|OSD_SMPTE))==(OSD_FRAME|OSD_SMPTE)?" ":"",
		(OSD_mode&OSD_SMPTE)?"SMPTE":""
		);
	  break;
	case 'o':		/* --offset */
	  ts_offset=atoi(optarg);
	  printf("set time offset to %li frames\n",ts_offset);
	  break;
	case 'k':		/* --keyframes */
	  seekflags=SEEK_KEY;
	  printf("seeking to keyframes only\n");
	  break;
	case 'K':		/* --anyframe */
	  seekflags=SEEK_CONTINUOUS;
#if LIBAVFORMAT_BUILD < 4617
	  printf("libavformat (ffmpeg) does not support continuous seeking...\n uprade your ffmpeg library and recompile xjadeo!\n");
#else
	  printf("enabled continuous seeking..\n");
#endif
	  break;
	case 'F':		/* --filefps */
          filefps = atof(optarg);
	  break;
	case 'f':		/* --fps */
          delay = 1.0 / atof(optarg);
	  break;
	case 'x':		/* --vo --videomode */
          videomode = atoi(optarg);
	  if (videomode == 0) videomode = parsevidoutname(optarg);
	  break;
#ifdef HAVE_MIDI
	case 'm':		/* --midi */
	  strncpy(midiid,optarg,32);
	  midiid[31]=0;
	  break;
	case 'M':		/* --midifps */
          midi_clkconvert = atoi(optarg);
	  break;
#endif
	case 'V':
	  printversion();
	  exit(0);
	case 'a':
	  start_ontop=1;
	  break;
	case 'h':
	  usage (0);
	default:
	  usage (EXIT_FAILURE);
    }
  }
  return optind;
}

static void
usage (int status)
{
  printf ("%s - \
jack video monitor\n", program_name);
  printf ("usage: %s [Options] <video-file>\n", program_name);
  printf ("       %s -R [Options] [<video-file>]\n", program_name);
  printf (""
"Options:\n"
"  -q, --quiet, --silent     inhibit usual output\n"
"  -v, --verbose             print more information\n"
#ifdef HAVE_MQ
"  -R, --remote              remote control (stdin) - implies non verbose&quiet\n"
#else
"  -R, --remote              set-up message queue for xjremote\n"
#endif
"  -f <val>, --fps <val>     video display update fps - default 10.0 fps\n"
"  -k, --keyframes           seek to keyframes only\n"
"  -K, --continuous          decode video source continuously. (extra latency\n"
"                            when seeking to non-key frames.)\n"
"  -o <int>, --offset <int>  add/subtract <int> video-frames to/from timecode\n"
"  -x <int>, --vo <int>,     set the video output mode (default: 0 - autodetect\n"
"      --videomode <int>     -1 prints a list of available modes.\n"
"  -i <int> --info <int>     render OnScreenDisplay info: 0:off, %i:frame,\n"
"                            %i:smpte, %i:both. (use remote ctrl for more opts.)\n",
	OSD_FRAME,OSD_SMPTE,OSD_FRAME|OSD_SMPTE); // :)

  printf (""
#ifdef HAVE_MIDI
#ifdef HAVE_PORTMIDI
"  -m <int>, --midi <int>,   use portmidi instead of jack (-1: autodetect)\n"
"                            value > -1 specifies a (input) midi port to use\n" 	  
"                            use -v -m -1 to list midi ports.\n" 	  
#else /* alsa midi */
"  -m <port>,                use alsamidi instead of jack\n"
"      --midi <port>,        specify alsa seq id to connect to. (-1: none)\n" 	  
"                            eg. -m ardour or -m 80 \n"
#endif /* HAVE_PORTMIDI */
"  -M <int>, --midifps <int> how to 'convert' MTC SMPTE to framenumber:\n"
"                            0: use framerate of MTC clock (default)\n" 
"                            2: use video file FPS\n" 
"                            3: resample: videoFPS / MTC \n" 
#endif /* HAVE_MIDI */
"  -a, --ontop               stack xjadeo window on top of the desktop.\n"
"                            requires xv vidmode and EWMH.\n"
"  -t, --try-codec           checks if the video-file can be played by jadeo.\n"
"                            exits with code 1 if the file is not supported.\n"
"			     no window is opened in this mode.\n"
#ifdef HAVE_LASH
"  -L, --nolash              ignore the fact that xjadeo could use lash.\n"
"  --lash-no-autoresume	     [liblash option]\n"
"  --lash-no-start-server    [liblash option]\n"
#endif /* HAVE_LASH */
"  -h, --help                display this help and exit\n"
"  -V, --version             output version information and exit\n"
"  \n"
"  Check the docs to learn how the video should be encoded.\n"
);
  exit (status);
}

static void printversion (void) {
  printf ("xjadeo version %s [ ", VERSION);
#ifndef HAVE_MIDI
  printf("no MIDI ");
#else /* have Midi */
# ifdef HAVE_PORTMIDI
  printf("portmidi ");
# else /* alsa midi */
  printf("alsa-midi ");
# endif 
# ifdef HAVE_LASH
  printf("LASH ");
# endif 
  printf("]\n compiled with LIBAVFORMAT_BUILD 0x%x = %i\n", LIBAVFORMAT_BUILD, LIBAVFORMAT_BUILD);
#endif /* HAVE_MIDI */
  printf(" displays: "
#if HAVE_LIBXV
		"Xv "
#endif 
#if HAVE_SDL
		"SDL "
#endif 
#if HAVE_IMLIB
		"X11/imlib "
#endif 
#if HAVE_IMLIB2
		"X11/imlib2"
# ifdef IMLIB2RGBA
		"(RGBA32) "
# else 
		"(RGB24) "
# endif 
#endif 
		"\n"
  );
}

void stat_osd_fontfile(void) {
#ifdef HAVE_FT
  struct stat s;
  strcpy(OSD_fontfile,FONT_FILE);

  if (stat(OSD_fontfile, &s)==0 ) {
    if (want_verbose) fprintf(stdout,"OSD font file: %s\n",OSD_fontfile);
      return;
  }
  if (!want_quiet)
    fprintf(stderr,"no TTF font found. OSD will not be available until you set one.\n");
#endif
}

//--------------------------------------------
// Main
//--------------------------------------------

int
main (int argc, char **argv)
{
  int i;
  int lashed = 0; // did we get started by lashd ?
  char*   movie= NULL;

  program_name = argv[0];

  xjadeorc(); // read config files - default values before parsing cmd line.

#ifdef HAVE_LASH
  for (i=0;i<argc;i++) if (!strncmp(argv[i],"--lash-id",9)) lashed=1;
  lash_args_t *lash_args = lash_extract_args(&argc, &argv);
#endif

  i = decode_switches (argc, argv);

#ifdef HAVE_LASH
//  if (!lashed && avoid_lash == 0)  {
  lash_client = lash_init (lash_args, PACKAGE_NAME,
                     0  | LASH_Config_Data_Set  
                     ,LASH_PROTOCOL (2,0));
  if (!lash_client) {
    fprintf(stderr, "could not initialise LASH!\n");
  } else {
    lash_setup();
    if (!want_quiet) printf("Connected to LASH.\n");
  }
  //lash_args_destroy(lash_args);
#endif

  if (videomode < 0) vidoutmode(videomode); // dump modes and exit.

  if ((i+1)== argc) movie = argv[i];
  else if (remote_en && i==argc) movie = "";
  else usage (EXIT_FAILURE);

  if (want_verbose) printf ("xjadeo %s\n", VERSION);

  if (lashed==1 && remote_en) {
    printf("xjadeo remote-ctrl disabled (resuming a LASH session).\n");
    remote_en=0;
  }

  stat_osd_fontfile();
    
  /* do the work */
  avinit();

  // format needs to be set before calling init_moviebuffer
  render_fmt = vidoutmode(videomode);

  // only try to seek to frame 1 and decode it.
  if (try_codec) do_try_this_file_and_exit (movie);


  open_movie(movie);
  
  open_window(&argc,&argv);

 // try fallbacks if window open failed in autodetect mode
  if (videomode==0 && getvidmode() ==0) { // re-use cmd-option variable as counter.
    if (want_verbose) printf("trying video driver fallbacks.\n");
    while (getvidmode() ==0) { // check if window is open.
      videomode++;
      int tv=try_next_vidoutmode(videomode);
      if (tv<0) break; // no videomode found!
      if (want_verbose) printf("trying videomode: %i: %s\n",videomode,vidoutname(videomode));
      if (tv==0) continue; // this mode is not available
      render_fmt = vidoutmode(videomode);
      open_window(&argc,&argv); 
    }
  }

  if (getvidmode() ==0) {
	fprintf(stderr,"Could not open display.\n");
  	if(!remote_en) {
		// FIXME: cleanup close jack, midi and file ??
		exit(1);
	}
  }

  init_moviebuffer();

#ifdef HAVE_MIDI
  if (atoi(midiid) < -1 ) {
    open_jack();
  } else {
    midi_open(midiid);
  }
#else
  open_jack();
#endif

  if(remote_en) open_remote_ctrl();

  display_frame(0LL,1);
  
  event_loop();

  if(remote_en) close_remote_ctrl();
  
  close_window();
  
  close_movie();

#ifdef HAVE_MIDI
  if (midi_connected()) midi_close(); 
  else
#endif
  close_jack();
  
  if (!want_quiet)
    fprintf(stdout, "\nBye!\n");

  exit (0);
}

/* vi:set ts=8 sts=2 sw=2: */
