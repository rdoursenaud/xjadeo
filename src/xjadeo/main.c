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
 * xjadeo:  (c) 2006 - 2012
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

#ifdef OLD_FFMPEG
#include <avcodec.h> // needed for PIX_FMT 
#include <avformat.h>
#else
#include <libavcodec/avcodec.h> // needed for PIX_FMT 
#include <libavformat/avformat.h>
#endif

#include <getopt.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

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

int               movie_width  = 320;
int               movie_height = 180;
int               ffctv_width  = 320;
int               ffctv_height = 180;
float             movie_aspect = 320.0 / 180.0;
AVFormatContext   *pFormatCtx = NULL;
int               videoStream = -1;
AVCodecContext    *pCodecCtx = NULL;
AVFrame           *pFrame = NULL;
AVFrame           *pFrameFMT = NULL;
uint8_t           *buffer = NULL;

int               render_fmt = PIX_FMT_YUV420P; ///< needs to be set before calling movie_open

/* Video File Info */
double	duration = 1;
double	framerate = 1;
long	frames = 1;
int64_t file_frame_offset = 0;

/* Option flags and variables */
char	*current_file = NULL;
char    *smpte_offset = NULL;
long 	ts_offset = 0;
long 	userFrame = 0; // seek to this frame if jack and midi are N/A
long 	dispFrame = 0; // global strorage... = (SMPTE+offset) with boundaries to [0..movie_file_frames]
int 	force_redraw = 0;

int 	interaction_override = 0; // disable some options.
/* 1 (bit 0) : ignore 'q' and 'ESC' quit key
 * 2 (bit 1) : igore window-manager close button
 * 4 (bit 2) : ignore OSX-Menu QUIT
 * 8 (bit 3) : ignore left-mouse click
 * 16(bit 4) : disable '+' '-' '{' '}' offset keys
 */

int want_quiet   =0;	/* --quiet, --silent */
int want_debug   =0;	/* -D --debug  (hidden option) */
int want_verbose =0;	/* --verbose */
int want_avverbose =0;	/* --avverbose */
int want_genpts =0;	/* --genpts */
int want_ignstart =0;	/* --ignorefileoffset */
int want_nosplash =0;	/* --nosplash */
int start_ontop =0;	/* --ontop // -a */
int start_fullscreen =0;/* --fullscreen // -s */
int want_letterbox =0;  /* --letterbox -b */
int want_dropframes =0; /* --dropframes -N  -- force using drop-frame timecode */
int want_autodrop =1;   /* --nodropframes -n (hidden option) -- allow using drop-frame timecode */
int avoid_lash   =0;	/* --nolash */
int remote_en =0;	/* --remote, -R */
int osc_port =0;	/* --osc, -O */
int mq_en =0;		/* --mq, -Q */
char *ipc_queue = NULL; /* --ipc, -W */
int remote_mode =0;	/* 0: undirectional ; >0: bidir
			 * bitwise enable async-messages 
			 *  so far only: 
			 *   (1) notify timecode 
			 *   (2) notify changed timecode 
			 */

int try_codec =0;	/* --try-codec */

#ifdef HAVE_MIDI
char midiid[128] = "-2";/* --midi # -1: autodetect -2: jack-transport, -3: none/userFrame */
int midi_clkconvert =0;	/* --midifps [0:MTC|1:VIDEO|2:RESAMPLE] */
char *midi_driver = NULL; /* --mididriver */
#endif

int have_dropframes =0; /* detected from MTC;  TODO: force to zero if jack or user TC */
int jack_clkconvert =1; /* --jackfps  - NOT YET IMPLEMENTED
                          [0:audio_frames_per_video_frame
                           1:video-file] */
int use_jack = 1;
#if defined (HAVE_LTCSMPTE) || defined (HAVE_LTC)
int use_ltc = 0;        /* -l , --ltc */
#endif 
char *load_rc = NULL;
char *load_movie = NULL;
#ifdef JACK_SESSION
char *jack_uuid = NULL;
int jack_session_restore = 0;
int js_winx = -1;
int js_winy = -1;
int js_winw = -1;
int js_winh = -1;
#endif 

#ifdef HAVE_LASH
lash_client_t *lash_client;
#endif

#ifdef HAVE_MACOSX
// mac GUI defaults
int seekflags    = SEEK_CONTINUOUS; 
#else
int seekflags    = SEEK_ANY; 
#endif

int     midi_clkadj =1;	/* --midiclk  */
double 	filefps = -1.0; // if > 0 override autodetected video file frame rate
int	videomode = 0; // --vo <int>  - default: autodetect
double 	delay = -1; // use file's FPS


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
  {"avverbose", no_argument, 0, 'A'},
  {"genpts", no_argument, 0, 'P'},
  {"ignorefileoffset", no_argument, 0, 'I'},
  {"nofileoffset", no_argument, 0, 'I'},
  {"nosplash", no_argument, 0, 'S'},
  {"keyframes", no_argument, 0, 'k'},
  {"continuous", required_argument, 0, 'K'},
  {"offset", no_argument, 0, 'o'},
  {"fps", required_argument, 0, 'f'},
  {"filefps", required_argument, 0, 'F'},
  {"videomode", required_argument, 0, 'x'},
  {"vo", required_argument, 0, 'x'},
  {"remote", no_argument, 0, 'R'},
  {"mq", no_argument, 0, 'Q'},
  {"ipc", required_argument, 0, 'W'},
  {"help", no_argument, 0, 'h'},
  {"version", no_argument, 0, 'V'},
  {"try-codec", no_argument, 0, 't'},
  {"info", no_argument, 0, 'i'},
  {"ontop", no_argument, 0, 'a'},
  {"fullscreen", no_argument, 0, 's'},
  {"nolash", required_argument, 0, 'L'},
  {"dropframes", no_argument, 0, 'N'},
  {"nodropframes", no_argument, 0, 'n'},
  {"letterbox", no_argument, 0, 'b'},
  {"midi", required_argument, 0, 'm'},
  {"midifps", required_argument, 0, 'M'},
  {"midi-driver", required_argument, 0, 'd'},
  {"midiclk", no_argument, 0, 'C'},
  {"no-midiclk", no_argument, 0, 'c'},
  {"ltc", no_argument, 0, 'l'},
  {"ttf-font", required_argument, 0, 'T'},
#ifdef JACK_SESSION
  {"uuid", required_argument, 0, 'U'},
  {"rc", required_argument, 0, 'r'},
#endif
#ifdef HAVE_LIBLO
  {"osc", required_argument, 0, 'O'},
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
			   "A"	/* avverbose */
			   "P"	/* genpts */
			   "I"	/* ignorefileoffset */
			   "h"	/* help */
			   "S"	/* nosplash */
			   "R"	/* stdio remote control */
			   "Q"	/* POSIX rt-message queues */
			   "W:"	/* IPC message queues */
			   "k"	/* keyframes */
			   "K"	/* anyframe */
			   "o:"	/* offset */
			   "t"	/* try-codec */
			   "T:"	/* ttf-font */
			   "f:"	/* fps */
			   "F:"	/* file FPS */
			   "x:"	/* video-mode */
			   "a"	/* always on top */
			   "s"	/* start in full-screen mode */
			   "i:"	/* info - OSD-mode */
			   "b"	/* letterbox */
			   "m:"	/* midi interface */
			   "M:"	/* midi clk convert */
			   "d:"	/* midi driver */
			   "C"	/* --midiclk */
			   "c"	/* --no-midiclk */
			   "l"	/* --ltc */
#ifdef JACK_SESSION
			   "r:"	/* --rc */
			   "U:"	/* --uuid */
#endif
			   "N"	/* --dropframes */
			   "n"	/* --nodropframes */
#ifdef HAVE_LIBLO
			   "O:"	/* --osc  */
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
	  want_avverbose = 0;
	  break;
	case 'D':		/* --debug */
	  want_debug = 1;
	  break;
	case 'A':		/* --avverbose */
	  want_avverbose = !remote_en;
	  break;
	case 'v':		/* --verbose */
	  want_verbose = !remote_en;
	  break;
	case 'S':		/* --nosplash */
	  want_nosplash = 1;
	  break;
	case 'I':		/* --ignorefileoffset */
	  want_ignstart++;
	  break;
	case 'P':		/* --avverbose */
	  want_genpts = 1;
	  break;
	case 'R':		/* --remote */
	  remote_en = 1;
	  want_quiet = 1;
	  want_verbose = 0;
	  want_avverbose = 0;
	  break;
	case 'Q':		/* --mq */
	  mq_en = 1;
	  break;
	case 'W':		/* --ipc */
	  if (ipc_queue) free(ipc_queue);
	  ipc_queue = strdup(optarg);
	  break;
	case 'O':		/* --avverbose */
	  osc_port=atoi(optarg);
	  break;
	case 'n':		/* --nodropframes */
	  want_autodrop = 0;
	  break;
	case 'N':		/* --dropframes */
	  want_dropframes = 1;
	  break;
	case 'L':		/* --nolash */
	  avoid_lash = 1;
	  break;
	case 'b':		/* --letterbox */
	  want_letterbox = 1;
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
	  // we don't know the file's framerate yet!
	  smpte_offset=strdup(optarg); 
	  //ts_offset=smptestring_to_frame(optarg,0);
	  //printf("set time offset to %li frames\n",ts_offset);
	  break;
	case 'k':		/* --keyframes */
	  seekflags=SEEK_KEY;
	  printf("seeking to keyframes only\n");
	  break;
	case 'K':		/* --anyframe */
	  seekflags=SEEK_CONTINUOUS;
	  if (!want_quiet)
#if LIBAVFORMAT_BUILD < 4617
	  printf("libavformat (ffmpeg) does not support continuous seeking...\n uprade your ffmpeg library and recompile xjadeo!\n");
#else
	  printf("enabled continuous seeking..\n");
#endif
	  break;
	case 'F':		/* --filefps */
	  if(atof(optarg)>0)
	    filefps = atof(optarg); // TODO: use av_parse_video_frame_rate()
	  break;
	case 'f':		/* --fps */
	  if(atof(optarg)>0)
	    delay = 1.0 / atof(optarg);
	  else delay = -1; // use file-framerate
	  break;
	case 'x':		/* --vo --videomode */
          videomode = atoi(optarg);
	  if (videomode == 0) videomode = parsevidoutname(optarg);
	  break;
#ifdef HAVE_MIDI
	case 'm':		/* --midi */
	  strncpy(midiid,optarg,sizeof(midiid));
	  midiid[(sizeof(midiid)-1)]=0;
	  break;
	case 'd':		/* --midi-driver */
	  if (midi_driver) free(midi_driver);
          midi_driver = strdup(optarg);
	  break;
	case 'M':		/* --midifps */
          midi_clkconvert = atoi(optarg);
	  break;
	case 'C':		/* --midiclk */
          midi_clkadj = 1;
	  break;
	case 'c':		/* --no-midiclk */
          midi_clkadj = 0;
	  break;
#else
	case 'm':		/* --midi */
	case 'd':		/* --midi-driver */
	case 'M':		/* --midifps */
	case 'C':		/* --midiclk */
	case 'c':		/* --no-midiclk */
	  printf("This version of xjadeo is compiled without MIDI support\n");
	  break;
#endif
#if defined (HAVE_LTCSMPTE) || defined (HAVE_LTC)
	case 'l':		/* --ltc */
          use_ltc = 1;
	  break;
#else
	case 'l':		/* --ltc */
	  printf("This version of xjadeo is compiled without LTC support\n");
	  break;
#endif
	case 'U':		/* --uuid */
#ifdef JACK_SESSION
          if (jack_uuid) free(jack_uuid);
          jack_uuid = strdup(optarg);
#endif
	  break;
	case 'r':		/* --rc */
          if (load_rc) free(load_rc);
          load_rc = strdup(optarg);
	  break;
	case 'V':
	  printversion();
	  exit(0);
	case 's':
	  start_fullscreen=1;
	  break;
	case 'a':
	  start_ontop=1;
	  break;
	case 'T':
	  strcpy(OSD_fontfile, optarg);
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
"  -h, --help                display this help and exit\n"
"  -V, --version             print version information and exit\n"
"  -q, --quiet, --silent     inhibit usual output\n"
"  -v, --verbose             print more information\n"
"\n"
"  -A, --avverbose           dump ffmpeg messages.\n"
"  -a, --ontop               stack xjadeo window on top of the desktop.\n"
"                            requires x11 or xv videomode and EWMH.\n"
"  -b, --letterbox           retain apect ratio when scaling (Xv only).\n"
"  -c, --no-midiclk          ignore MTC quarter frames.\n"
"  -d <name>,                specify midi-driver to use. run 'xjadeo -V' to\n"
"     --midi-driver <name>   list supported driver(s). <name> is not case-\n"
"                            sensitive and can be shortened to the first unique\n"
"                            name. eg '-d j' for jack, '-d alsa-r' for alsa-raw\n"
"  -f <val>, --fps <val>     display update freq. - default -1 use file's fps\n"
"  -i <int>, --info <int>    render OnScreenDisplay info: 0:off, %i:frame,\n"
"                            %i:smpte, %i:both. (use remote ctrl for more opts.)\n"
"",	OSD_FRAME,OSD_SMPTE,OSD_FRAME|OSD_SMPTE); // :)
  printf ("" /* take a breath */
"  -I, --ignorefileoffset    set the beginning of the file to SMPTE zero.\n"
"                            eg. override timestamps of split vob files.\n"
"  -k, --keyframes           seek to keyframes only\n"
"  -K, --continuous          decode video source continuously. (extra latency\n"
"                            when seeking to non-key frames.)\n"
#ifdef HAVE_LASH
"  -L, --nolash              ignore the fact that xjadeo could use lash.\n"
"  --lash-no-autoresume      [liblash option]\n"
"  --lash-no-start-server    [liblash option]\n"
#endif /* HAVE_LASH */
"  -l, --ltc                 sync to LinearTimeCode (audio-jack).\n"
#ifdef JACK_SESSION
"  -U, --uuid                specify JACK-SESSION UUID.\n"
#endif
"  -m <port>,                use MTC instead of jack-transport\n"
"      --midi <port>         <port> argument is driver-specific:\n"
"                            * jack-midi: specify midi-port name to connect to\n"
"                              or \"\" to not auto-connect.\n"
"                            * alsa-seq: specify id to connect to. (-1: none)\n"
"                              eg. -m ardour or -m 80 \n"
"                            * portmidi: numeric-id; -1: autodetect\n"
"                            value > -1 specifies a (input) midi port to use\n" 	  
"                            use '-v -m -1' to list midi-ports.\n" 	  
"                            * alsa-raw: specify device-name \n"
"                              eg. -m hw:1,0 or -m 1 \n"
"  -M <int>,                 how to 'convert' MTC SMPTE to framenumber:\n"
"      --midifps <int>       0: use framerate of MTC clock (default)\n" 
"                            2: use video file FPS\n" 
"                            3: \"resample\": videoFPS / MTC \n" 
/* - undocumented /hidden/ options
"  -n , --nodropframes       parse MTC as announced, but do not use frame-drop\n" 
"                            algorithm for OSD - useful for debugging\n"
"  -N , --dropframes         force the SMPTE converter to use the drop-frames\n" 
"                            algorithm. (Frame dropping is only useful in \n"
"                            combination with a 29fps MIDI time source.\n"
"                            MTC in 29.97-frame-drop format is automatically\n"
"                            detected and it is illegal to use this algorithm\n"
"                            for other framerates.) DO NOT USE THIS OPTION,\n"
"                            unless you really know what you are doing.\n" 
*/
"  -o <int>, --offset <int>  add/subtract <int> video-frames to/from timecode\n"
#ifdef HAVE_LIBLO
"  -O <port>, --osc <port>   listen for OSC messages on given port.\n"
#endif
"  -P , --genpts             ffmpeg option - ignore timestamps in the file.\n"
#ifdef HAVE_MQ
"  -Q, --mq                  set-up RT message queues for xjremote\n"
#endif
"  -r <file>, --rc <file>    .rc settings file to load.\n"
"  -R, --remote              remote control (stdin) - implies non verbose&quiet\n"
"  -S, --nosplash            do not display splash image on startup.\n"
"  -s, --fullscreen          start xjadeo in fullscreen mode.\n"
"                            requires x11 or xv videomode.\n"
"  -t, --try-codec           checks if the video-file can be played by jadeo.\n"
"                            exits with code 1 if the file is not supported.\n"
"			     no window is opened in this mode.\n"
"  -T <file>,                \n"
"      --ttf-file <file>     path to .ttf font for on-screen-display\n"
#ifdef HAVE_IPCMSG
"  -W, --ipc                 set-up IPC message queues for xjremote\n"
#endif
"  -x <int>, --vo <int>,     set the video output mode (default: 0 - autodetect\n"
"      --videomode <int>     -1 prints a list of available modes.\n"
"  \n"
"  Check the docs to learn how the video should be encoded.\n"
);
  exit (status);
}

static void printversion (void) {
  printf ("xjadeo ");
  printf ("version %s ", VERSION);
#ifdef SUBVERSION
  if (strlen(SUBVERSION)>0 && strcmp(SUBVERSION, "exported")) {
    printf ("scm-%s ", SUBVERSION);
  }
#endif
  printf ("[ ");
#ifdef HAVE_LASH
  printf("LASH ");
#endif 
#if defined (HAVE_LTCSMPTE) || defined (HAVE_LTC)
  printf("LTC ");
#endif
#ifdef JACK_SESSION
  printf("JACK-SESSION ");
#endif
#ifdef HAVE_MQ
  printf("POSIX-MQueue ");
#elif defined HAVE_IPCMSG
  printf("IPC-MSG ");
#endif
  printf("]\n compiled with ffmpeg: AVFORMAT=0x%x AVCODEC=0x%x\n", 
      LIBAVFORMAT_BUILD, LIBAVCODEC_BUILD);
  printf(" MTC-MIDI: ");
#ifndef HAVE_MIDI
  printf("disabled.");
#else /* have Midi */
# ifdef HAVE_JACKMIDI
  printf("jack-midi ");
# endif 
# ifdef ALSA_SEQ_MIDI
  printf("alsa-sequencer ");
# endif 
# ifdef HAVE_PORTMIDI
  printf("portmidi ");
# endif 
# ifdef ALSA_RAW_MIDI
  printf("alsa-raw ");
# endif 
# if (defined ALSA_RAW_MIDI || defined HAVE_PORTMIDI || defined ALSA_SEQ_MIDI || defined HAVE_JACKMIDI)
  printf("(first listed is default)");
# else
  printf("no midi-driver available.");
# endif
#endif /* HAVE_MIDI */
  printf("\n displays: "
#if HAVE_LIBXV
		"Xv "
#endif 
#ifdef HAVE_SDL
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
#ifdef HAVE_MACOSX
		"OSX/quartz "
#endif 
		"\n"
  );
}

void stat_osd_fontfile(void) {
#ifdef HAVE_FT
  struct stat s;

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

void clean_up (int status) {

  if(remote_en) close_remote_ctrl();
#ifdef HAVE_MQ
  if(mq_en) close_mq_ctrl();
#elif defined HAVE_IPCMSG
  if(ipc_queue) {
    close_ipcmsg_ctrl();
    free(ipc_queue);
  }
#endif
  shutdown_osc();

  close_window();
  
  close_movie();

#ifdef HAVE_MIDI
  if (midi_driver) free(midi_driver);
  if (midi_connected()) midi_close(); 
  else
#endif
#if defined (HAVE_LTCSMPTE) || defined (HAVE_LTC)
  if (ltcjack_connected()) close_ltcjack(); 
  else
#endif
  close_jack();
  free_freetype();

  if (smpte_offset) free(smpte_offset);
  if (load_rc) free(load_rc);
  if (load_movie) free(load_movie);
  
  if (!want_quiet)
    fprintf(stdout, "\nBye!\n");
  exit(status);
}

void catchsig (int sig) {
#ifndef HAVE_WINDOWS
  signal(SIGHUP, catchsig); /* reset signal */
  signal(SIGINT, catchsig);
//signal(SIGHUP, SIG_IGN); /* reset signal */
//signal(SIGINT, SIG_IGN);
#endif
  if (!want_quiet)
    fprintf(stdout,"\n CAUGHT SIGINT - shutting down.\n");
  loop_flag=0;
  clean_up(1);
  exit(1);
}

int
main (int argc, char **argv)
{
  int i;
  int lashed = 0; // did we get started by lashd ?
  char*   movie= NULL;

  program_name = argv[0];

  xjadeorc(); // read config files - default values before parsing cmd line.

#ifdef __APPLE__
{
  if ( argc >= 2 && strncmp (argv[1], "-psn", 4) == 0 ) {
    char **gArgv = (char **) malloc(sizeof (char *) * argc);
    int i;
    gArgv[0] = argv[0];
    for (i=1;i<argc-1;++i) {
      gArgv[i] = argv[i+1];
    }
    --argc;
    argv=gArgv;
  }
}
#endif

#ifdef HAVE_LASH
  for (i=0;i<argc;i++) if (!strncmp(argv[i],"--lash-id",9)) lashed=1;
  lash_args_t *lash_args = lash_extract_args(&argc, &argv);
#endif

  i = decode_switches (argc, argv);

#ifdef HAVE_LASH
  if (avoid_lash == 0)  { ///< check if this works as promised
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
  }
#endif

  if (videomode < 0) vidoutmode(videomode); // dump modes and exit.

  if ((i+1)== argc) movie = argv[i];
  else if ((remote_en || mq_en || ipc_queue || osc_port || load_rc) && i==argc) movie = "";
#ifndef HAVE_MACOSX
  else usage (EXIT_FAILURE);
#else
  else movie = "";
#endif

  if (load_rc) {
    if (testfile(load_rc)) readconfig(load_rc);
    if (load_movie) movie = load_movie;
  }

  if (want_verbose) printf ("xjadeo %s\n", VERSION);

  if (lashed==1 && remote_en) {
    printf("xjadeo remote-ctrl is unavailable when resuming a LASH session\n");
    remote_en=0;
  }

  stat_osd_fontfile();
    
  /* do the work */
  avinit();

  // format needs to be set before calling init_moviebuffer
  render_fmt = vidoutmode(videomode);

  // only try to seek to frame 1 and decode it.
  if (try_codec) do_try_this_file_and_exit (movie);
#ifndef HAVE_WINDOWS
  signal (SIGHUP, catchsig);
  signal (SIGINT, catchsig);
#endif

  if (osc_port > 0) initialize_osc(osc_port);

  open_movie(movie);
  
  open_window();

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
      open_window(); 
    }
  }

  if (getvidmode() ==0) {
	fprintf(stderr,"Could not open display.\n");
  	if(!remote_en) {  // && !mq_en && !ipc_queue) { /* TODO: allow windowless startup with MQ ?! */
#ifdef HAVE_MIDI
                if (midi_driver) free(midi_driver);
                if (midi_connected()) midi_close(); 
                else
#endif
#if defined (HAVE_LTCSMPTE) || defined (HAVE_LTC)
                if (ltcjack_connected()) close_ltcjack(); 
                else
#endif
		close_jack();
		exit(1);
	}
  }

  init_moviebuffer();

#ifdef HAVE_MIDI
  midi_choose_driver(midi_driver);

#ifdef JACK_SESSION
  if (jack_uuid && !strcmp(midi_driver_name(), "JACK-MIDI")) {
    // don't auto-connect jack-midi on session restore.
    if (atoi(midiid) == 0) midiid[0]='\0';
  }
#endif

  if (atoi(midiid) >= -1 ) {
    if (!want_quiet) 
      printf("using MTC as sync-source.\n");
    midi_open(midiid);
  } else 
#endif
#if defined (HAVE_LTCSMPTE) || defined (HAVE_LTC)
  if (use_ltc) {
    open_ltcjack(NULL);
  } else
#endif
  {
    if (!want_quiet) 
      printf("using JACK-transport as sync source.\n");
    if (use_jack)
      open_jack();
  }

#ifdef HAVE_MQ
  if(mq_en) open_mq_ctrl();
#elif defined HAVE_IPCMSG
  if(ipc_queue) open_ipcmsg_ctrl(ipc_queue);
#endif
  if(remote_en) open_remote_ctrl();

  display_frame(0LL,1,1);
  splash(buffer); 

  event_loop();
  
  clean_up(0);
  return (0);
}

/* vi:set ts=8 sts=2 sw=2: */
