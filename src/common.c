/******************************************************
 Talamasca
 by Jeroen Massar <jeroen@unfix.org>
 (C) Copyright Jeroen Massar 2004 All Rights Reserved
 http://unfix.org/projects/talamasca/
*******************************************************
 $Author: $
 $Id: $
 $Date: $
*******************************************************
 Common routines for Talamasca
******************************************************/

#include "talamasca.h"

/* getline debugging? */
/*#define E(x) x*/
#define E(x) {}

void dologA(int level, char *module, const char *fmt, va_list ap)
{
	if (g_conf && !g_conf->verbose && level == LOG_DEBUG) return;
	if (g_conf && g_conf->daemonize) vsyslog(LOG_LOCAL7|level, fmt, ap);
	else
	{
		if (g_conf && g_conf->verbose)
		{
			printf("[%6s : %7s] ",
				level == LOG_DEBUG ?	"debug" :
				(level == LOG_ERR ?	"error" :
				(level == LOG_WARNING ?	"warn" :
				(level == LOG_INFO ?	"info" : ""))),
				module);
		}
		else if (level == LOG_ERR) printf("Error: ");
		vprintf(fmt, ap);
	}
}

void dolog(int level, char *module, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	dologA(level, module, fmt, ap);
	va_end(ap);
}

int huprunning()
{
	int pid;

	FILE *f = fopen(PIDFILE, "r");
	if (!f) return 0;
	fscanf(f, "%d", &pid);
	fclose(f);
	/* If we can HUP it, it still runs */
	return (kill(pid, SIGHUP) == 0 ? 1 : 0);
}

void savepid()
{
	FILE *f = fopen(PIDFILE, "w");
	if (!f) return;
	fprintf(f, "%d", getpid());
	fclose(f);

	dolog(LOG_INFO, "common", "Running as PID %d\n", getpid());
}

void cleanpid(int i)
{
	if (g_conf && g_conf->quit == false) dolog(LOG_INFO, "common", "Trying to exit...\n");
	unlink(PIDFILE);
	if (g_conf) g_conf->quit = true;
}

int sock_printfA(SOCKET sock, const char *fmt, va_list ap)
{
	char		buf[2048];
	unsigned int	len = 0;
	int		sent = 0;

	/* When not a (connected) socket send it to the logs */
	if (sock == -1) dologA(LOG_INFO, "common", fmt, ap);
	else
	{
		/* Format the string */
		len = vsnprintf(buf, sizeof(buf), fmt, ap);

		/* Send the line(s) over the network */
		sent = send(sock, buf, len, 0);

		/* Show this as debug output? */
		if (g_conf->verbose)
		{
			/* Strip the last \n */
			len = (int)strlen(buf);
			if (len > 0) buf[len-1] = '\0';
			/* dump the information */
			dolog(LOG_DEBUG, "common", "sock_printf (%03x) : \"%s\"\n", sock, buf);
		}
	}
	return sent;
}


int sock_printf(SOCKET sock, const char *fmt, ...)
{
	int i;
	va_list ap;

	va_start(ap, fmt);
	i = sock_printfA(sock, fmt, ap);
	va_end(ap);
	return i;
}

/* Read a line from a socket and store it in ubuf
 * Note: uses internal caching, this should be the only function
 * used to read from the sock! The internal cache is rbuf.
 */
int sock_getline(SOCKET sock, char *rbuf, unsigned int rbuflen, unsigned int *filled, char *ubuf, unsigned int ubuflen)
{
	int		i = 0;
	unsigned int	j = 0;
	
	/* A closed socket? -> clear the buffer */
	if (sock == -1)
	{
		memset(rbuf, 0, rbuflen);
		*filled = 0;
		return -1;
	}

	/* Clear the caller supplied buffer, just in case */
	memset(ubuf, 0, ubuflen);

	for (;;)
	{
		E(dolog(LOG_DEBUG, "common", "gl() - Filled %u\n", *filled);)

		/* Did we still have something in the buffer? */
		if (*filled > 0)
		{
			E(dolog(LOG_DEBUG, "common", "gl() - Seeking newline\n");)

			/* Walk to the end or until we reach a \n */
			for (j=0; (j < (*filled-1)) && (rbuf[j] != '\n'); j++);

			E(dolog(LOG_DEBUG, "common", "gl() - Seeking newline - end\n");)

			/* Did we find a newline? */
			if (rbuf[j] == '\n')
			{
				E(dolog(LOG_DEBUG, "common", "gl() - Found newline at %u\n", j+1);)

				/* Newline with a Linefeed in front of it ? -> remove it */
				if (rbuf[j] == '\n' && rbuf[j-1] == '\r')
				{
					E(dolog(LOG_DEBUG, "common", "gl() - Removing LF\n");)
					j--;
				}
				E(else dolog(LOG_DEBUG, "common", "gl() - No LF\n");)

				if (j >= ubuflen)
				{
					dolog(LOG_ERR, "common", "UBuffer almost flowed over without receiving a newline (%u > %u)\n", j, ubuflen);
					return -1;
				}

				/* Copy this over to the caller */
				memcpy(ubuf, rbuf, j);

				E(dolog(LOG_DEBUG, "common", "gl() - Copied %u bytes from %x to %x\n", j, rbuf, ubuf);)

				/* Count the \r if it is there */
				if (rbuf[j] == '\r') j++;
				/* Count the \n */
				j++;

				/* filled = what is left in the buffer */
				*filled -= j;
				
				E(dolog(LOG_DEBUG, "common", "gl() - %u bytes left in the buffer\n", *filled);)

				/* Now move the rest of the buffer to the front */
				if (*filled > 0) memmove(rbuf, &rbuf[j], *filled);
				else *filled = 0;

				/* Show this as debug output */
				if (g_conf->verbose) dolog(LOG_DEBUG, "common", "sock_getline(%03x) : \"%s\"\n", sock, ubuf);

				/* We got ourselves a line in 'buf' thus return to the caller */
				return j;
			}
		}

		E(dolog(LOG_DEBUG, "common", "gl() - Trying to receive (max=%u)...\n", rbuflen-*filled-10);)

		/* Fill the rest of the buffer */
		i = recv(sock, &rbuf[*filled], rbuflen-*filled-10, 0);

		E(dolog(LOG_DEBUG, "common", "gl() - Received %d\n", i);)

		/* Fail on errors */
		if (i <= 0)
		{
			if (errno == EAGAIN)
			{
				/* printf("[strace] returning 0 (%d)\n", errno); */
				return 0;
			}
			/* printf("[strace] returning -1 (%d)\n", errno); */
			return -1;
		}

		/* We got more filled space! */
		*filled+=i;

		/* Buffer overflow? */
		if (*filled >= (rbuflen-8))
		{
			dolog(LOG_ERR, "common", "RBuffer almost flowed over without receiving a newline\n");
			return -1;
		}

		/* And try again in this loop ;) */
	}

	/* Never reached */
	return -1;
}

/* Connect this client to a server */
SOCKET connect_client(const char *hostname, const char *service, int family, int socktype)
{
	SOCKET		sock = -1;
	struct addrinfo	hints, *res, *ressave;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = family;
	hints.ai_socktype = socktype;

	if (getaddrinfo(hostname, service, &hints, &res) != 0)
	{
		dolog(LOG_ERR, "common", "Couldn't resolve host %s, service %s\n", hostname, service);
		return -1;
	}

	ressave = res;

	while (res)
	{
		sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (sock == -1) continue;
		if (connect(sock, res->ai_addr, (unsigned int)res->ai_addrlen) == 0) break;
		closesocket(sock);
		sock = -1;
		res = res->ai_next;
	}

	freeaddrinfo(ressave);

	/* Enable non-blocking operation */
	fcntl(sock, F_SETFL, O_NONBLOCK);

	return sock;
}

/* Count the number of fields in <s> */
unsigned int countfields(char *s)
{
	int	n = 1, i;
	bool	hasquote = false;

	if (s == NULL) return 0;
	for (i=0; s[i] != '\0'; i++)
	{
		if (s[i] == '"')
		{
			hasquote = !hasquote;
			continue;
		}
		if ( !hasquote && s[i] == ' ') n++;
	}
	return n;
}

/*
 * Copy <count> fields starting with <n> of string <s> into <buf> with a maximum of buflen
 * First field is 1
 */
bool copyfields(char *s, unsigned int field, unsigned int count, char *buf, unsigned int buflen)
{
	unsigned int begin = 0, i = 0, f = field, c = count;
	bool hasquote = false;

	/* Clear the buffer */
	memset(buf, 0, buflen);

	/* Copy at least 1 field */
	if (c > 0) c--;

	while (s[i] != '\0')
	{
		/*
		 * When the beginning is not found yet update it
		 * and proceed to the next field
		 */
		if (f > 0)
		{
			f--;
			begin = i;
		}
		/* We found another field */
		else if (c > 0) c--;

		for (;s[i] != '\0'; i++)
		{
			if (s[i] == '"')
			{
				if (hasquote)
				{
					hasquote = false;
					break;
				}
				hasquote = true;
				continue;
			}
			if (!hasquote && s[i] == ' ') break;
		}

		if (f == 0 && (c == 0 || count == 0))
		{
			/* Skip the quote */
			if (s[begin] == '"') begin++;
			/* When only n fields where requested */
			if (count != 0) i-=begin;
			/* User wanted everything */
			else i=(unsigned int)strlen(s)-begin;

			/* Copy it to the supplied buffer as long as it fits */
			strncpy(buf, s+begin, i > buflen ? buflen : i);
#if 0
			dolog(LOG_DEBUG, "common", "copyfield() : '%s', begin = %u, len = %u (\"%s\", f=%u, c=%u)\n", buf, begin, i, s, field, count);
#endif
			return true;
		}
		
		i++;
	}
	dolog(LOG_WARNING, "common", "copyfield() - Field %u+%u didn't exist in '%s'\n", field, count, s);
	return false;
}
