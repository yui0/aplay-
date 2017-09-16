/* public domain Simple, Minimalistic, kbhit function
 *	©2017 Yuichiro Nakada
 * */

#include <termios.h>
#include <unistd.h>   // for read()

static struct termios initial_settings, new_settings;
//static int peek_character = -1;

void init_keyboard()
{
	tcgetattr(0, &initial_settings);
	new_settings = initial_settings;
	new_settings.c_lflag &= ~(ICANON|ECHO);
	/*new_settings.c_lflag &= ~ICANON;
	new_settings.c_lflag &= ~ECHO;
	new_settings.c_lflag &= ~ISIG;*/
//	new_settings.c_cc[VMIN] = 1;
	new_settings.c_cc[VMIN] = 0;
	new_settings.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &new_settings);
}

void close_keyboard()
{
	tcsetattr(0, TCSANOW, &initial_settings);
}

#define GETCH_BUF_LEN 256
int getch2_len;
unsigned char getch2_buf[GETCH_BUF_LEN];

int kbhit()
{
/*	unsigned char ch;
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

	return 0;*/

}

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

//https://raw.githubusercontent.com/larsmagne/mplayer/master/osdep/getch2.c
	int retval = read(0, &getch2_buf[getch2_len], GETCH_BUF_LEN-getch2_len);
	if (retval < 1) return 0;
	return getch2_buf[0];
	/*getch2_len += retval;

	while (getch2_len > 0 && (getch2_len > 1 || getch2_buf[0] != 27)) {
		int i, len, code;
		len = 1;
		code = getch2_buf[0];

		getch2_len -= len;
		for (i=0; i<getch2_len; i++) getch2_buf[i] = getch2_buf[len+i];
	}*/
}

