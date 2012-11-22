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
 Central include and struct file
******************************************************/

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <syslog.h>
#include <pwd.h>
#include <getopt.h>
#include <fcntl.h>

#define PIDFILE "/var/run/talamasca.pid"
#define BUFFERSIZE 2048

#ifdef DEBUG
#define D(x) x
#else
#define D(x) {}
#endif

#include "linklist.h"

/* Booleans */
#define false	0
#define true	(!false)
#define bool	int

/* Socket */
#ifndef SOCKET
#define SOCKET int
#endif
#define closesocket close

/* server */
enum states
{
	SS_DISCONNECTED = 0,
	SS_AUTHENTICATING,
	SS_CONNECTED
};

enum srv_types
{
	SRV_USER,		/* User linkup, thus we are actually a user on this server */
	SRV_BITLBEE,		/* BitlBee linkup, thus we are actually a user on this server and need to apply tricks */
	SRV_RFC1459,		/* RFC1459 server<->server protocol */
	SRV_TS,			/* Timestamped (http://www.alleged.com/irc/timestamp/) */
	SRV_P10			/* P10 server<->server protocol (http://www.xs4all.nl/~carlo17/irc/P10.html) */
};

/* Our configuration structure */
struct conf
{
	fd_set			selectset;			/* Select set */
	int			hifd;				/* Highest FD in use */
	unsigned int		numsocks;			/* Number of sockets still open */
	time_t			boottime;			/* Bootup time */
	char			*config_file;			/* Configuration file */
	
	char			*service_name;			/* Global name of this service */
	char			*service_description;		/* Global description of this service */
	char			*motd_file;			/* The file containing the MOTD */
	char			*admin_location1;		/* Administrative Location 1 */
	char			*admin_location2;		/* Administrative Location 2 */
	char			*admin_email;			/* Administrative Email address */
	unsigned char		*config_password;		/* Configuration password */

	struct list		*servers;			/* Servers */
	struct list		*users;				/* Users */

	bool			daemonize;			/* To Daemonize or to not to Daemonize */
	bool			verbose;			/* Verbose Operation ? */
	bool			quit;				/* Global Quit signal */

	bool			bitlbee_auto_add;		/* true = !add automatic, false = user must do !add */
};

/* Global Stuff */
extern struct conf *g_conf;

/* common */
void dolog(int level, char *module, const char *fmt, ...);
int huprunning();
void savepid();
void cleanpid(int i);
int sock_printfA(SOCKET sock, const char *fmt, va_list ap);
int sock_printf(SOCKET sock, const char *fmt, ...);
int sock_getline(SOCKET sock, char *rbuf, unsigned int rbuflen, unsigned int *filled, char *ubuf, unsigned int ubuflen);
SOCKET connect_client(const char *hostname, const char *port, int family, int socktype);
unsigned int countfields(char *s);
bool copyfields(char *s, unsigned int n, unsigned int count, char *buf, unsigned int buflen);
#define copyfield(s,n,buf,buflen) copyfields(s,n,1,buf,buflen)

/* config */
bool cfg_fromfile_direct(char *file);

/* MD5 */
#define md5byte unsigned char
#define UWORD32 u_int32_t

struct MD5Context
{
        UWORD32 buf[4];
        UWORD32 bytes[2];
        UWORD32 in[16];
};

void MD5Init(struct MD5Context *context);
void MD5Update(struct MD5Context *context, md5byte const *buf, unsigned len);
void MD5Final(unsigned char digest[16], struct MD5Context *context);
void MD5Transform(UWORD32 buf[4], UWORD32 const in[16]);


/* A server */
struct server
{
	enum srv_types	type;			/* Type: true = uplink (server), false = user */
	char		*tag;			/* Server Tag */
	char		*hostname;		/* Server hostname */
	char		*port;			/* Server port */
	char		*nickname;		/* Nickname */
	char		*name;			/* Username or Servername */
	char		*password;		/* Password */
	char		*identity;		/* The identity this server has */
	char		*description;		/* Description */

	struct user	*user;			/* Server user (when in user mode) */
	struct channel	*defaultchannel;	/* The channel to map our users onto */

	time_t		lastconnect;		/* Last time we tried to connect */
	SOCKET		socket;			/* The socket */
	enum states	state;			/* Server State */

	char		buffer[BUFFERSIZE];	/* Read buffer */
	unsigned int	bufferfill;		/* How far the buffer is filled */

	struct list	*users;			/* Users (struct serveruser) */
	struct list	*channels;		/* Channels (struct channel) */

	/* Bitlbee support */
	char		*bitlbee_identifypass;	/* The password to identify our account on the BitlBee server */

	/* Statistics (eg for "stats u") */
	uint64_t	stat_sent_msg,		/* Number of messages sent */
			stat_sent_bytes,	/* Number of bytes sent */
			stat_recv_msg,		/* Number of messages received */
			stat_recv_bytes;	/* Number of bytes received */
};

/* A user on a server */
struct serveruser
{
	bool		config;		/* Configuration item? */
	struct server	*server;	/* The server */
	struct user	*user;		/* The user */
	bool		introduced;	/* Did we introduce this user already? */
};

/* user */
struct user
{
	bool		config;		/* Configuration item? */

	char		*nick;		/* The nick of the user on this server */
	char		*ident;		/* Ident of the user */
	char		*host;		/* Host of the user */
	char		*realname;	/* Realname of the user */
	char		*away;		/* Away message */

	struct server	*server;	/* On which server this user lives */
	struct list	*channels;	/* Channels this user is on (struct channel) */
	
	time_t		lastmessage;	/* Last message */
};

/* channel */
struct channel
{
	bool		config;		/* Configuration item? */

	char		*tag;		/* Channel Tag */
	char		*name;		/* Channel name */

	struct server	*server;	/* The server this channel lives on */
	struct list	*users;		/* Users on this channel (channeluser) */

	char		*topic;		/* The topic of the channel */
	char		*topic_who;	/* Who set the topic */
	time_t		topic_when;	/* When the topic was set */

	struct channel	*link;		/* To which channel this channel is linked */

	char		*key;		/* Channel key */

	bool		f_anonymous;	/* Anonymous channel */
	bool		f_invite;	/* Invite only channel */
	bool		f_moderated;	/* Moderated channel */
	bool		f_nooutside;	/* No outside messages */
	bool		f_private;	/* Private */
	bool		f_secret;	/* Secret */
	bool		f_reop;		/* Re-op */
	bool		f_topiclock;	/* Topic Lock */
	int		limit;		/* User limit (-1 = none) */
};

/* A user on a channel */
struct channeluser
{
	bool		config;		/* Configuration item? */

	struct channel	*channel;	/* The channel */
	struct user	*user;		/* The user */
	bool		introduced;	/* Did we introduce this user already? */

	bool		f_creator;	/* User created the channel */
	bool		f_operator;	/* User has ops */
	bool		f_voice;	/* User has voice */
};

/* Server */
void server_printf(struct server *server, const char *fmt, ...);
struct server *server_find_tag(char *tag);
struct server *server_add(char *tag, enum srv_types type, char *hostname, char *port, char *nickname, char *name, char *password, char *identity, char *description);
void server_destroy(struct server *server);
void server_disconnect(struct server *server);
void server_connect(struct server *server);
void server_handle(struct server *server);
void server_user_change_nick(struct server *server, struct user *user, char *oldnick);
struct serveruser *server_introduce(struct server *server, struct user *user);
void server_leave(struct server *server, struct user *user, char *reason, bool kill);

/* User */
struct user *user_add(char *nick, struct server *server, bool config);
void user_destroy(struct user *user, char *reason);
void user_introduce(struct user *user);
void user_resetidle(struct user *user);
struct user *user_find_nick(char *nick);
void user_change_away(struct user *user, char *reason);
void user_change_nick(struct user *user, char *newnick, bool local);
void user_change_ident(struct user *user, char *ident);
void user_change_host(struct user *user, char *host);
void user_change_realname(struct user *user, char *realname);

/* Channel */
struct channel *channel_find_tag(char *tag);
struct channel *channel_add(struct server *server, char *name, char *tag);
void channel_destroy(struct channel *channel);
void channel_link(struct channel *channel, struct channel *link);
struct channeluser *channel_find_user(struct channel *channel, struct user *user);
void channel_message(struct channel *channel, struct user *user, char *message, ...);
void channel_adduser(struct channel *channel, struct user *user);
void channel_deluser(struct channel *channel, struct user *user, char *reason, bool notify);
void channel_change_topic(struct channel *channel, char *who);
void channel_change_topic_who(struct channel *channel, char *who);
void channel_change_topic_when(struct channel *channel, time_t when);
void channel_change_key(struct channel *channel, char *key);
