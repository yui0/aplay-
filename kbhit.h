/* public domain Simple, Minimalistic, kbhit function
 *	©2022 Yuichiro Nakada
 * */

#include <sys/ioctl.h>
#include <termios.h>

int kbhit()
{
	static const int STDIN = 0;
	static int initialized = 0;

	if (!initialized) {
		// Use termios to turn off line buffering
		struct termios term;
		tcgetattr(STDIN, &term);
		term.c_lflag &= ~ICANON;
		tcsetattr(STDIN, TCSANOW, &term);
		setbuf(stdin, NULL);
		initialized = 1;
	}

	int bytesWaiting;
	ioctl(STDIN, FIONREAD, &bytesWaiting);
	return bytesWaiting;
}

#if 0

#if 0
#include <termios.h>
static struct termios t_old, t_new;
void initTermios(int echo)
{
	tcgetattr(0, &t_old);
	t_new = t_old;
	t_new.c_lflag &= ~ICANON; // disable buffered i/o
	if (echo) {
		t_new.c_lflag |= ECHO; // set echo mode
	} else {
		t_new.c_lflag &= ~ECHO; // set no echo mode
	}
	tcsetattr(0, TCSANOW, &t_new);
}
void resetTermios()
{
	tcsetattr(0, TCSANOW, &t_old);
}
int getch_(int echo)
{
	char ch;
	initTermios(echo);
	ch = getchar();
	resetTermios();
	return ch;
}
// Read 1 character without echo
int _getch()
{
	return getch_(0);
}
// Read 1 character with echo
int _getche()
{
	return getch_(1);
}

#define readch	_getch
#define init_keyboard()
#define close_keyboard()

#else
#include <termios.h>
#include <unistd.h>   // for read()

static struct termios initial_settings, new_settings;
//static int peek_character = -1;

void init_keyboard()
{
	tcgetattr(0, &initial_settings);
	new_settings = initial_settings;
	new_settings.c_lflag &= ~(ECHO | ECHONL | ICANON);
	new_settings.c_cc[VMIN] = 0;
	new_settings.c_cc[VTIME] = 0;

//	new_settings.c_iflag &= ~ICRNL;	// ! \r -> \n
//	new_settings.c_oflag &= ~ONLCR;	// ! \n -> \r
	new_settings.c_lflag &= ~ISIG;	// ignore signal

	new_settings.c_iflag &= ~IXOFF;	// control c-s, c-q [input]
	new_settings.c_iflag &= ~IXON;	// control c-s, c-q [output]

	tcsetattr(0, TCSANOW, &new_settings);
}

void close_keyboard()
{
	tcsetattr(0, TCSANOW, &initial_settings);
}

#define GETCH_BUF_LEN 256
int getch2_len;
unsigned char getch2_buf[GETCH_BUF_LEN];

/*int kbhit()
{
	unsigned char ch;
	int nread;

	if (peek_character != -1) {
		return 1;
	}

	new_settings.c_cc[VMIN]=0;
	tcsetattr(0, TCSANOW, &new_settings);
	nread = read(0, &ch, 1);
	new_settings.c_cc[VMIN]=1;
	tcsetattr(0, TCSANOW, &new_settings);

	if (nread == 1) {
		peek_character = ch;
		return 1;
	}

	return 0;
}*/

int readch()
{
/*	char ch;

	if (peek_character != -1) {
		ch = peek_character;
		peek_character = -1;
		return ch;
	}
	read(0, &ch, 1);

	return ch;*/

	unsigned char ch;
	read(0, &ch, 1);
	return ch;

	// https://raw.githubusercontent.com/larsmagne/mplayer/master/osdep/getch2.c
/*	int retval = read(0, &getch2_buf[getch2_len], GETCH_BUF_LEN-getch2_len);
	if (retval < 1) return 0;
	return getch2_buf[0];*/
	/*getch2_len += retval;

	while (getch2_len > 0 && (getch2_len > 1 || getch2_buf[0] != 27)) {
		int i, len, code;
		len = 1;
		code = getch2_buf[0];

		getch2_len -= len;
		for (i=0; i<getch2_len; i++) getch2_buf[i] = getch2_buf[len+i];
	}*/
}
#endif

#endif
