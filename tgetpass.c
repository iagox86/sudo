/*
 *  CU sudo version 1.6
 *  Copyright (c) 1996, 1998, 1999 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 1, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Please send bugs, changes, problems to sudo-bugs@courtesan.com
 *
 *******************************************************************
 *
 *  This module contains tgetpass(), getpass(3) with a timeout.
 *  It should work on any OS that supports sgtty (4BSD), termio (SYSV),
 *  or termios (POSIX) line disciplines.
 *
 *  Todd C. Miller  Sun Jun  5 17:22:31 MDT 1994
 */

#include "config.h"

#include <stdio.h>
#ifdef STDC_HEADERS
#include <stdlib.h>
#endif /* STDC_HEADERS */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#ifdef HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif /* HAVE_STRINGS_H */
#include <limits.h>
#include <pwd.h>
#include <sys/param.h>
#include <sys/types.h>
#ifdef HAVE_SYS_BSDTYPES_H
#include <sys/bsdtypes.h>
#endif /* HAVE_SYS_BSDTYPES_H */
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif /* HAVE_SYS_SELECT_H */
#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#else
#ifdef HAVE_TERMIO_H
#include <termio.h>
#else
#include <sgtty.h>
#include <sys/ioctl.h>
#endif /* HAVE_TERMIO_H */
#endif /* HAVE_TERMIOS_H */

#include "sudo.h"

#ifndef TCSASOFT
#define TCSASOFT	0
#endif /* TCSASOFT */

#ifndef lint
static const char rcsid[] = "$Sudo$";
#endif /* lint */


/******************************************************************
 *
 *  tgetpass()
 *
 *  this function prints a prompt and gets a password from /dev/tty
 *  or stdin.  Echo is turned off (if possible) during password entry
 *  and input will time out based on the value of timeout.
 */

char *
tgetpass(prompt, timeout, echo_off)
    const char *prompt;
    int timeout;
    int echo_off;
{
#ifdef HAVE_TERMIOS_H
    struct termios term;
#else
#ifdef HAVE_TERMIO_H
    struct termio term;
#else
    struct sgttyb ttyb;
#endif /* HAVE_TERMIO_H */
#endif /* HAVE_TERMIOS_H */
#ifdef POSIX_SIGNALS
    sigset_t set, oset;
#else
    int omask;
#endif /* POSIX_SIGNALS */
    int n, input, output;
    static char buf[SUDO_PASS_MAX + 1];
    fd_set *readfds;
    struct timeval tv;

    /*
     * mask out SIGINT and SIGTSTP, should probably just catch and deal.
     */
#ifdef POSIX_SIGNALS
    (void) sigemptyset(&set);
    (void) sigaddset(&set, SIGINT);
    (void) sigaddset(&set, SIGTSTP);
    (void) sigprocmask(SIG_BLOCK, &set, &oset);
#else
    omask = sigblock(sigmask(SIGINT)|sigmask(SIGTSTP));
#endif /* POSIX_SIGNALS */

    /*
     * open /dev/tty for reading/writing if possible or use
     * stdin and stderr instead.
     */
    if ((input = output = open(_PATH_TTY, O_RDWR)) == -1) {
	input = STDIN_FILENO;
	output = STDERR_FILENO;
    }

    /* print the prompt */
    if (prompt)
	(void) write(output, prompt, strlen(prompt) + 1);

    /*
     * turn off echo unless asked to keep it on
     */
    if (echo_off) {
#ifdef HAVE_TERMIOS_H
	(void) tcgetattr(input, &term);
	if ((echo_off = (term.c_lflag & ECHO))) {
	    term.c_lflag &= ~ECHO;
	    (void) tcsetattr(input, TCSAFLUSH|TCSASOFT, &term);
	}
#else
#ifdef HAVE_TERMIO_H
	(void) ioctl(input, TCGETA, &term);
	if ((echo_off = (term.c_lflag & ECHO))) {
	    term.c_lflag &= ~ECHO;
	    (void) ioctl(input, TCSETA, &term);
	}
#else
	(void) ioctl(input, TIOCGETP, &ttyb);
	if ((echo_off = (ttyb.sg_flags & ECHO))) {
	    ttyb.sg_flags &= ~ECHO;
	    (void) ioctl(input, TIOCSETP, &ttyb);
	}
#endif /* HAVE_TERMIO_H */
#endif /* HAVE_TERMIOS_H */
    }

    /*
     * Timeout of <= 0 means no timeout
     */
    if (timeout > 0) {
	/* setup for select(2) */
	n = howmany(input + 1, NFDBITS) * sizeof(fd_mask);
	readfds = (fd_set *) emalloc(n);
	(void) memset((VOID *)readfds, 0, n);
	FD_SET(input, readfds);

	/* set timeout for select */
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	/*
	 * get password or return empty string if nothing to read by timeout
	 */
	buf[0] = '\0';
	while ((n = select(input + 1, readfds, 0, 0, &tv)) == -1 &&
	    errno == EINTR)
	    ;
	if (n != 0 && (n = read(input, buf, sizeof(buf) - 1)) > 0) {
	    if (buf[n - 1] == '\n')
		n--;
	    buf[n] = '\0';
	}
	free(readfds);
    } else {
	buf[0] = '\0';
	if ((n = read(input, buf, sizeof(buf) - 1)) > 0) {
	    if (buf[n - 1] == '\n')
		n--;
	    buf[n] = '\0';
	}
    }

     /* turn on echo if we turned it off above */
#ifdef HAVE_TERMIOS_H
    if (echo_off) {
	term.c_lflag |= ECHO;
	(void) tcsetattr(input, TCSAFLUSH|TCSASOFT, &term);
    }
#else
#ifdef HAVE_TERMIO_H
    if (echo_off) {
	term.c_lflag |= ECHO;
	(void) ioctl(input, TCSETA, &term);
    }
#else
    if (echo_off) {
	ttyb.sg_flags |= ECHO;
	(void) ioctl(input, TIOCSETP, &ttyb);
    }
#endif /* HAVE_TERMIO_H */
#endif /* HAVE_TERMIOS_H */

    /* print a newline since echo is turned off */
    if (echo_off)
	(void) write(output, "\n", 1);

    /* restore old signal mask */
#ifdef POSIX_SIGNALS
    (void) sigprocmask(SIG_SETMASK, &oset, NULL);
#else
    (void) sigsetmask(omask);
#endif /* POSIX_SIGNALS */

    /* close /dev/tty if that's what we opened */
    if (input != STDIN_FILENO)
	(void) close(input);

    return(buf);
}
