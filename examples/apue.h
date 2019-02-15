/*
 * Our own header, to be included before all standard system headers.
 */
#ifndef	_APUE_H
#define	_APUE_H

#define _POSIX_C_SOURCE 200809L

#if defined(SOLARIS)		/* Solaris 10 */
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 700
#endif

#include <sys/types.h>		/* some systems still require this */
#include <sys/stat.h>
#include <sys/termios.h>	/* for winsize */
#if defined(MACOS) || !defined(TIOCGWINSZ)
#include <sys/ioctl.h>
#endif

#include <stdio.h>		/* for convenience */
#include <stdlib.h>		/* for convenience */
#include <stddef.h>		/* for offsetof */
#include <string.h>		/* for convenience */
#include <unistd.h>		/* for convenience */

int		 serv_listen(const char *);			/* {Prog servlisten_sockets} */
int		 serv_accept(int, uid_t *);			/* {Prog servaccept_sockets} */
int		 cli_conn(const char *);			/* {Prog cliconn_sockets} */
void	 sleep_us(unsigned int);			/* {Ex sleepus} */


#endif	/* _APUE_H */
