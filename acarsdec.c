#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <sched.h>
#include "acarsdec.h"

channel_t channel[MAXNBCHANNELS];
unsigned int nbch;

char *idstation;
int inmode = 0;
int verbose = 0;
int outtype = 2;
int netout;
int airflt = 0;
int mdly=600;

#ifdef WITH_RTL
int gain = 1000;
int ppm = 0;
#endif
#ifdef WITH_AIR
int gain = 10;
#endif

char *Rawaddr = NULL;
char *logfilename = NULL;

static void usage(void)
{
	fprintf(stderr,
		"\n\n");
	fprintf(stderr,
		"Usage: acarsdec  [-v] [-o lv] [-t time] [-A] [-n ipaddr:port] [-l logfile]");
#ifdef WITH_ALSA
	fprintf(stderr, " -a alsapcmdevice  |");
#endif
#ifdef WITH_SNDFILE
	fprintf(stderr, " -f inputwavfile  |");
#endif
#ifdef WITH_RTL
	fprintf(stderr,
		" [-g gain] [-p ppm] -r rtldevicenumber  f1 [f2] ... [fN]");
#endif
	fprintf(stderr, " -R < /stdin@%d:1channel:float32native  |",INTRATE);
	fprintf(stderr, "\n\n");
	fprintf(stderr, " -v\t\t\t: verbose\n");
	fprintf(stderr,
		" -A\t\t\t: don't display uplink messages (ie : only aircraft messages)\n");
	fprintf(stderr,
		"\n -o lv\t\t\t: output format : 0: no log, 1 one line by msg., 2 full (default) , 3 monitor mode, 4 newline separated JSON\n");
	fprintf(stderr,
		"\n -t time\t\t\t: set forget time (TTL) in seconds for monitor mode (default=600s)\n");
	fprintf(stderr,
		" -l logfile\t\t: Append log messages to logfile (Default : stdout).\n");
	fprintf(stderr,
		" -n ipaddr:port\t\t: send acars messages to addr:port on UDP in planeplotter compatible format\n");
	fprintf(stderr,
		" -N ipaddr:port\t\t: send acars messages to addr:port on UDP in acarsdev native format\n");
	fprintf(stderr,
		" -i stationid\t\t: station id used in acarsdec network format.\n\n");
#ifdef WITH_ALSA
	fprintf(stderr,
		" -a alsapcmdevice\t: decode from soundcard input alsapcmdevice (ie: hw:0,0)\n");
#endif
#ifdef WITH_SNDFILE
	fprintf(stderr,
		" -f inputwavfile\t: decode from a wav file at %d sampling rate\n",INTRATE);
#endif
#ifdef WITH_RTL
	fprintf(stderr,
		" -g gain\t\t: set rtl preamp gain in tenth of db (ie -g 90 for +9db). By default use AGC\n");
	fprintf(stderr, " -p ppm\t\t\t: set rtl ppm frequency correction\n");
	fprintf(stderr,
		" -r rtldevice f1 [f2]...[f%d]\t: decode from rtl dongle number or S/N rtldevice receiving at VHF frequencies f1 and optionally f2 to f%d in Mhz (ie : -r 0 131.525 131.725 131.825 )\n", MAXNBCHANNELS, MAXNBCHANNELS);
#endif
	fprintf(stderr,
		" -R \t: decode from stdin at %d sampling rate, 1 channel, native float32\n",INTRATE);
#ifdef WITH_AIR
	fprintf(stderr,
		" -s f1 [f2]...[f%d]\t: decode from airspy receiving at VHF frequencies f1 and optionally f2 to f%d in Mhz (ie : -r 0 131.525 131.725 131.825 )\n", MAXNBCHANNELS, MAXNBCHANNELS);
#endif
	fprintf(stderr,
		"\nFor any input source, up to %d channels may be simultaneously decoded\n", MAXNBCHANNELS);
	exit(1);
}

static void sighandler(int signum)
{
	fprintf(stderr, "Signal caught, exiting!\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int c;
	int res, n;
	struct sigaction sigact;

	while ((c = getopt(argc, argv, "vafrsRo:t:g:Ap:n:N:l:c:i:f:")) != EOF) {

		switch (c) {
		case 'v':
			verbose = 1;
			break;
		case 'o':
			outtype = atoi(optarg);
			break;
		case 't':
			mdly = atoi(optarg);
			break;
#ifdef WITH_ALSA
		case 'a':
			res = initAlsa(argv, optind);
			inmode = 1;
			break;
#endif
#ifdef WITH_SNDFILE
		case 'f':
			res = initSoundfile(argv, optind);
			inmode = 2;
			break;
#endif
#ifdef WITH_RTL
		case 'r':
			res = initRtl(argv, optind);
			inmode = 3;
			break;
		case 'p':
			ppm = atoi(optarg);
			break;
    case 'g':
			gain = atoi(optarg);
			break;
#endif
#ifdef WITH_AIR
		case 's':
			res = initAirspy(argv, optind);
			inmode = 4;
			break;
#endif
		case 'R':
			res = initRaw(argv, optind);
			inmode = 5;
			break;
		case 'n':
			Rawaddr = optarg;
			netout = 0;
			break;
		case 'N':
			Rawaddr = optarg;
			netout = 1;
			break;
		case 'A':
			airflt = 1;
			break;
		case 'l':
			logfilename = optarg;
			break;
		case 'i':
			idstation = strndup(optarg,8);
			break;

		default:
			usage();
		}
	}

	if (inmode == 0) {
		fprintf(stderr, "Need at least one of -a|-f|-r|-R options\n");
		usage();
	}

	if (res) {
		fprintf(stderr, "Unable to init input\n");
		exit(res);
	}

	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);

	for (n = 0; n < nbch; n++) {
		channel[n].chn = n;

		res = initMsk(&(channel[n]));
		if (res)
			break;
		res = initAcars(&(channel[n]));
		if (res)
			break;
	}

	if (res) {
		fprintf(stderr, "Unable to init internal decoders\n");
		exit(res);
	}

	res = initOutput(logfilename, Rawaddr);
	if (res) {
		fprintf(stderr, "Unable to init output\n");
		exit(res);
	}


	if(outtype==3) {
		verbose=0;
		cls();
	}

	if (verbose)
		fprintf(stderr, "Decoding %d channels\n", nbch);

	/* main decoding  */
	switch (inmode) {
#ifdef WITH_ALSA
	case 1:
		res = runAlsaSample();
		break;
#endif
#ifdef WITH_SNDFILE
	case 2:
		res = runSoundfileSample();
		break;
#endif
#ifdef WITH_RTL
	case 3:
		res = runRtlSample();
		break;
#endif
#ifdef WITH_AIR
	case 4:
		res = runAirspySample();
		break;
#endif
	case 5:
		res = runRawSample();
		break;
	default:
		res = -1;
	}

	for (n = 0; n < nbch; n++)
		deinitAcars(&(channel[n]));

	exit(res);

}
