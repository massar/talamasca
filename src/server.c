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
 Server Handling
******************************************************/

#include "talamasca.h"

void server_printf(struct server *server, const char *fmt, ...)
{
	int i;
	va_list ap;

	va_start(ap, fmt);
	i = sock_printfA(server->socket, fmt, ap);
	va_end(ap);
	
	/* Update statistics */
	if (i > 0)
	{
		server->stat_sent_msg++;
		server->stat_sent_bytes+=i;
	}
}

struct server *server_find_tag(char *tag)
{
	struct server	*srv;
	struct listnode	*ln;

	LIST_LOOP(g_conf->servers, srv, ln)
	{
		if (strcasecmp(srv->tag, tag) == 0) return srv;
	}
	return NULL;
}

struct serveruser *server_find_user(struct server *server, struct user *user)
{
	struct serveruser	*su;
	struct listnode		*ln;
	
	if (!server)
	{
		dolog(LOG_ERR, "server", "server_find_user() - Something passed me a NULL server!\n");
		return NULL;
	}
	if (!user)
	{
		dolog(LOG_ERR, "server", "server_find_user() - Something passed me a NULL user!\n");
		return NULL;
	}

	LIST_LOOP(server->users, su, ln)
	{
		if (su->user == user) return su;
	}
	return NULL;
}

struct channel *server_find_channel(struct server *server, char *channel)
{
	struct channel	*ch;
	struct listnode	*cn;

	LIST_LOOP(server->channels, ch, cn)
	{
		if (strcasecmp(channel, ch->name) == 0) return ch;
	}
	return NULL;
}

struct serveruser *server_find_nick(struct server *server, char *nick)
{
	struct serveruser	*su;
	struct listnode		*ln;
	
	if (!server)
	{
		dolog(LOG_ERR, "server", "server_find_nick() - Something passed me a NULL server!\n");
		return NULL;
	}
	if (!nick)
	{
		dolog(LOG_ERR, "server", "server_find_nick() - Something passed me a NULL nick!\n");
		return NULL;
	}

	LIST_LOOP(server->users, su, ln)
	{
		if (strcasecmp(su->user->nick, nick) == 0) return su;
	}
	return NULL;
}

struct server *server_add(char *tag, enum srv_types type, char *hostname, char *port, char *nickname, char *name, char *password, char *identity, char *description)
{
	struct server *server = malloc(sizeof(*server));

	if (!server)
	{
		dolog(LOG_ERR, "server", "Not enough memory left to create a new server!?\n");
		exit(-1);
	}

	dolog(LOG_DEBUG, "server", "server_add(%s:%s)\n", hostname, port);

	/* Initialize */
	memset(server, 0, sizeof(*server));
	server->type		= type;
	server->socket		= -1;

	/* A server has users, who are globally unique, enforced through the global userlist */
	server->users		= list_new();
	server->users->del 	= NULL;

	/* A server has channels, not globally unique, but per server */
	server->channels	= list_new();
	server->channels->del 	= (void(*)(void *))channel_destroy;

	if (tag)		server->tag		= strdup(tag);
	if (hostname)		server->hostname	= strdup(hostname);
	if (port)		server->port		= strdup(port);
	if (nickname)		server->nickname	= strdup(nickname);
	if (name)		server->name		= strdup(name);
	if (password)		server->password	= strdup(password);
	if (identity)		server->identity	= strdup(identity);
	if (description)	server->description	= strdup(description);

	/* Add it */
	listnode_add(g_conf->servers, server);

	/* All okay */
	return server;
}

void server_destroy(struct server *server)
{
	struct server	*srv;
	struct listnode	*ln;

	if (!server)
	{
		dolog(LOG_DEBUG, "server", "server_disconnect() - Something passed me an empty server!\n");
		return;
	}
	
	dolog(LOG_DEBUG, "server", "Destroying server %s:%s\n",
		server->hostname, server->port);

	/* Cleanup */
	server_disconnect(server);
	
	/* Take us out of the server list */
	listnode_delete(g_conf->servers, server);

	/* Walk through the server list and unmap default channels */
	LIST_LOOP(g_conf->servers, srv, ln)
	{
		if (	!srv->defaultchannel ||
			srv->defaultchannel->server != server) continue;

		/* Deconfigure */
		dolog(LOG_WARNING, "server", "Deconfiguring %s:%s's default channel %s\n",
			srv->hostname, srv->port, srv->defaultchannel->name);
		srv->defaultchannel = NULL;
	}

	/* Free the node */
	list_delete(server->users);
	list_delete(server->channels);

	if (server->tag)			free(server->tag);
	if (server->hostname)			free(server->hostname);
	if (server->port)			free(server->port);
	if (server->nickname)			free(server->nickname);
	if (server->name)			free(server->name);
	if (server->password)			free(server->password);
	if (server->identity)			free(server->identity);
	if (server->description)		free(server->description);
	if (server->bitlbee_identifypass)	free(server->bitlbee_identifypass);

	free(server);
}

void server_connect(struct server *server)
{
	/* Only (re)connect when not connected
	 * or when the last time we tried was >(15) seconds ago
	 */
	if (	server->socket != -1 ||
		time(NULL) < server->lastconnect+(15))
	{
		dolog(LOG_DEBUG, "server", "Not reconnecting to %s:%s, last connect %u seconds ago\n",
			server->hostname, server->port, time(NULL)-server->lastconnect);
		return;
	}

	dolog(LOG_DEBUG, "server", "Trying to connect to %s:%s\n", server->hostname, server->port);

	/* Try to connect to the server */
	server->lastconnect = time(NULL);
	server->socket = connect_client(server->hostname, server->port, AF_UNSPEC, SOCK_STREAM);

	/* Failed? */
	if (server->socket == -1) return;

	/* We want to read this stuff */
	FD_SET(server->socket, &g_conf->selectset);
	if (server->socket > g_conf->hifd) g_conf->hifd = server->socket;
	g_conf->numsocks++;

	/* Send our login information */
	if (server->password)
	{
		server_printf(server, "PASS %s%s\n",
			server->password,
			server->type == SRV_TS ? " :TS" : "");
	}
	server_printf(server,
		server->type == SRV_USER || server->type == SRV_BITLBEE ?
			"USER %s . . :%s\n%s" :
			"SERVER %s 1 :%s\n%s",
		server->name, server->description,
		server->type == SRV_TS ? "CAPAB TS3\n" : "");
	if (server->type == SRV_USER || server->type == SRV_BITLBEE)
	{
		server_printf(server, "NICK %s\n", server->nickname);

		/* Create a user for this server */
		server->user = user_add(server->nickname, server, true);
		user_change_ident(server->user, server->name);
		user_change_host(server->user, server->hostname);
		user_change_realname(server->user, server->description);
		
		/* Introduce the user to the various servers */
		user_introduce(server->user);
	}

	/* State is authenticating */
	server->state = SS_AUTHENTICATING;
	dolog(LOG_DEBUG, "server", "%s:%s is now in state: authenticating\n", server->hostname, server->port);
}

char *getfreenick(char *tmp, unsigned int len)
{
	struct user *u = NULL;

	unsigned int i = 0;
	do
	{
		snprintf(tmp, len, "Ta%ula\n", i); 
		u = user_find_nick(tmp);
		i++;
	}
	while (u && i <= 9999);
	if (i <= 9999) return tmp;
	
	dolog(LOG_ERR, "server", "Couldn't create a free nickname...\n");
	return NULL;
}

bool is_nickokay(char *nick)
{
	/* Must start with an alphabetical char */
	if ((  (nick[0] >= 'A' &&
		nick[0] <= 'Z') ||
	       (nick[0] >= 'a' &&
		nick[0] <= 'z')) &&
		strlen(nick) < 10)
	{
		return true;
	}
	return false;
}

struct serveruser *server_introduce(struct server *server, struct user *user)
{
	struct serveruser *su;

	/* Try to find the user on the server */
	su = server_find_user(server, user);
	
	if (su && su->introduced)
	{
		dolog(LOG_DEBUG, "server", "User %s!%s@%s already introduced to server %s:%s\n",
			user->nick, user->ident, user->host,
			server->hostname, server->port);
		return su;
	}

	/* Not added yet? */
	if (!su)
	{
		su = malloc(sizeof(*su));
		if (!su)
		{
			dolog(LOG_ERR, "server", "server_introduce() Couldn't allocate memory for serveruser\n");
			exit(-42);
		}
		memset(su, 0, sizeof(*su));
		su->server = server;
		su->user = user;

		/* Add the user to the server list */
		listnode_add(server->users, su);
	}

	if (server->state != SS_CONNECTED)
	{
		dolog(LOG_DEBUG, "server", "Server %s:%s is not connected, thus cannot introduce user %s\n",
			server->hostname, server->port, user->nick);
		return su;
	}

	/* We'll introduce this person in a few...
	 * NOTE: This also excludes introduction of configured users on their own server
	 * as they have their own server in user->server
	 */
	su->introduced = true;

	/*
	 * - Do not introduce users on the server where they are on
	 * - Do not introduce server users
	 */
	if (	server == user->server ||
		user == user->server->user)
	{
		dolog(LOG_DEBUG, "server", "Not introducing user %s to it's own server\n", user->nick);
		return su;
	}

	/*
	 * Introduce users only to Server<->Server links
	 */
	if (	server->type == SRV_RFC1459 ||
		server->type == SRV_TS)
	{
		/* Introduce this user to the linked server */
		server_printf(server,
			"NICK %s 1 %u +i %s %s %s 0 :%s\n",
			user->nick, time(NULL), user->ident,
			user->host, server->name, user->realname);
	}
	else if (server->type == SRV_P10)
	{
		dolog(LOG_DEBUG, "server", "P10 is not implemented\n");
		exit(-42);
	}

	return su;
}

void server_leave(struct server *server, struct user *user, char *reason, bool kill)
{
	struct serveruser	*su;
	struct channel		*ch;
	struct listnode		*ln;

	/* Try to find the user on the server */
	su = server_find_user(server, user);

	if (!su)
	{
		dolog(LOG_DEBUG, "server", "User %s!%s@%s was not on server %s:%s\n",
			user->nick, user->ident, user->host,
			server->hostname, server->port);
		return;
	}

	/* Do we need to quit this user? */
	if (su->introduced)
	{	
		/*
		 * Can only let users leave when this is server link
		 */
		if (	server->type == SRV_RFC1459 ||
			server->type == SRV_TS)
		{
			if (!kill)
			{
				/* Quit this user from the server */
				server_printf(server,
					":%s QUIT :%s\n",
					user->nick, reason ? reason : "Leaving");
			}
		}
		else if (server->type == SRV_P10)
		{
			/* Not supported */
			exit(-42);
		}
		else if (server->type == SRV_USER ||
			 server->type == SRV_BITLBEE)
		{
			if (server->defaultchannel)
			{
				channel_message(server->defaultchannel, user,
					"### %s (%s@%s) quit (%s)\n",
					user->nick, user->ident, user->host, reason ? reason : "Leaving");
			}
		}
	}

	/* Remove the user from the channels she is on */
	while (user->channels->count > 0)
	{
		ch = NULL;
		LIST_LOOP(user->channels, ch, ln)
		{
			/* Only remove the user from channels on this server */
			if (ch->server != server) continue;

			/* Remove the user from the channel */
			channel_deluser(ch, user, reason, !kill);
			/* Start checking at the beginning as the list changed */
			break;
		}
		if (!ch) break;
	}

	/* Remove the user from the user list */
	listnode_delete(server->users, su);
}

/* Flush everything the server 'owns' */
void server_flush(struct server *server)
{
	struct serveruser	*su;
	struct channel		*ch;
	struct listnode		*ln, *ln2;

	if (!server)
	{
		dolog(LOG_DEBUG, "server", "server_flush() - Something passed me an empty server!\n");
		return;
	}

	/* Empty the users from the server */
	while (server->users->count > 0)
	{
		LIST_LOOP(server->users, su, ln)
		{
			/* Keep Configured users */
			if (su->user->config)
			{
				/* User has been quit from the server */
				su->introduced = false;
	
				/* Skip deletion as we want to keep this user */
				continue;
			}

			/* Local user? then quit them */
			if (su->user->server == server)
			{
				server_leave(server, su->user, "Flushing...", false);
			}
	
			/* Remove this serveruser from the list */
			listnode_delete(server->users, su);
			/* List changed so try again */
			break;
		}
		if (!ln) break;
	}

	/*
	 * By having the users leave the channels should be empty
	 * As we ignore information about non-configured channels
	 * we are back into prestine state
	 */
}

void server_disconnect(struct server *server)
{
	if (!server)
	{
		dolog(LOG_DEBUG, "server", "server_disconnect() - Something passed me an empty server!\n");
		return;
	}

	/* Flush the users from the server */
	server_flush(server);

	/* Last time we where connected */
	server->lastconnect = time(NULL);

	/* Don't try this when it is closed already */
	if (server->socket == -1) return;

	/* TODO: send a QUIT/ERROR ? */

	/* Cleanup the socket */
	FD_CLR(server->socket, &g_conf->selectset);
	closesocket(server->socket);
	server->socket = -1;
	g_conf->numsocks--;
}

void server_change_identity(struct server *server, char *identity)
{
	if (!server)
	{
		dolog(LOG_DEBUG, "server", "change_identity() Something passed me a NULL server\n");
		return;
	}

	if (server->identity)	free(server->identity);
	if (identity)		server->identity = strdup(identity);
	else			server->identity = NULL;
}

void server_change_description(struct server *server, char *description)
{
	if (!server)
	{
		dolog(LOG_DEBUG, "server", "change_description() Something passed me a NULL server\n");
		return;
	}

	if (server->description)	free(server->description);
	if (description)		server->description = strdup(description);
	else				server->description = NULL;
}

void server_user_change_nick(struct server *server, struct user *user, char *oldnick)
{
	struct channel	*ch;
	struct listnode	*cn;
	struct serveruser *su;

	/* Try to find the user on the server */
	su = server_find_user(server, user);
	
	if (!su)
	{
		dolog(LOG_WARNING, "server", "User %s!%s@%s was not introduced yet?! Introducing...\n",
			user->nick, user->ident, user->host);
		su = server_introduce(server, user);
	}

	if (	server->type == SRV_RFC1459 ||
		server->type == SRV_TS)
	{
		/* Change the nick of this user */
		server_printf(server,
			":%s NICK %s\n",
			oldnick, user->nick);
	}
	else if (server->type == SRV_P10)
	{
		/* Not implemented ;) */
		exit(-42);
	}
	else
	{
		/* Notify all the different channels on this server */
		LIST_LOOP(server->channels, ch, cn)
		{
			channel_message(ch, user, "### %s changed nick to %s", oldnick, user->nick);
		}
	}
}

void welcome_bitlbee_user(struct user *user)
{
	server_printf(user->server,
		"PRIVMSG %s :#########################################\n"
		"PRIVMSG %s :### Welcome to The Talamasca, %s\n"
		"PRIVMSG %s :### %s : %s\n"
		"PRIVMSG %s :### See !help for more information\n"
		"PRIVMSG %s :### and !motd for the message of the day.\n"
		"PRIVMSG %s :#########################################\n",
		user->nick, user->nick, user->nick, user->nick,
		g_conf->service_name, g_conf->service_description,
		user->nick, user->nick, user->nick);
}

#define IRCCMD_MAXPARAMS 42
struct irccmd
{
	char		*source;
	char		*ident;
	char		*host;
	struct user	*user;
	char		*cmd;
	unsigned int	numargs;
	char		*p[IRCCMD_MAXPARAMS];
};

/* Modifies line, inserting \0's, putting pointers into cmd */
bool server_parsestring(char *line, struct irccmd *cmd)
{
	char		*c = line, *p, *p2;
	unsigned int	pi = 0;

	/* Clear it out */
	memset(cmd, 0, sizeof(*cmd));

	/* Strip trailing spaces */
	for (pi=strlen(line)-1;pi>0;pi--)
	{
		if (line[pi] == ' ') line[pi] = '\0';
		else break;
	}

	pi = 0;

	/* Source given ? */
	if (*c == ':')
	{
		c++;
		cmd->source = c;

		/* Find ' ' - terminating the id */
		p2 = strchr(c, ' ');
		/* Find '!' - aka ident? */
		p = strchr(c, '!');
		/* Found && before the space ? -> terminate */
		if (p && p < p2)
		{
			/* Terminate the Nickname, by replacing the '!' */
			*p = '\0';
			c = &p[1];

			cmd->ident = c;

			/* Find the ident end '@' */
			p = strchr(c, '@');

			if (p)
			{
				*p = '\0';
				c = &p[1];
				cmd->host = c;
				/* Terminated below */
			}
		}

		/* Find seperating space */
		p = strchr(c, ' ');
		if (!p)
		{
			dolog(LOG_DEBUG, "parse", "no space found c=%x, line=%x\n", c, line);
			cmd->numargs = pi;
			return false;
		}

		/* Terminate */
		*p = '\0';

		/* The command is next */
		p++;
		c=p;
	}

	/* Command */
	cmd->cmd = c;

	/* Find space or end */
	p = strchr(c, ' ');
	if (p)
	{
		/* Terminate */
		*p = '\0';
	
		/* Go for the params */
		p++;
		c=p;
	
		/* Params */
		while (	c && p &&
			pi < IRCCMD_MAXPARAMS)
		{
			/*
			 * Rest of the line is the param?
			 * 
			 */
			if (*c == ':' || pi >= IRCCMD_MAXPARAMS)
			{
				c++;
				cmd->p[pi] = c;
				if (pi < IRCCMD_MAXPARAMS) pi++;
				break;
			}
	
			cmd->p[pi] = c;
			/* Find the space */
			p = strchr(c, ' ');
			if (p)
			{
				/* Terminate */
				*p = '\0';
				p++;
			}
	
			c = p;
			pi++;
		}
	}
	cmd->numargs = pi;

/*
	dolog(LOG_DEBUG, "parse", "src:%s, cmd:%s, num=%u, p0:%s, p1:%s, p2:%s, p3:%s, p4:%s, p5:%s\n",
		cmd->source, cmd->cmd, cmd->numargs, cmd->p[0], cmd->p[1], cmd->p[2], cmd->p[3], cmd->p[4], cmd->p[5]);
*/

	/* Try to find the user belonging to this message */
	/* FIXME: Verify that the origin is correct by comparing user->server */
	if (cmd->source) cmd->user = user_find_nick(cmd->source);

	return true;
}

/* Handle messages sent from the BitlBee 'root' user */
void server_handle_bitlbee_root(struct server *server, struct irccmd *cmd)
{
	char		*c = NULL, tmp[1024];
	struct user	*u;
	unsigned int	i;

	if (strncasecmp(cmd->p[1], "User `", 6) == 0 &&
		strstr(cmd->p[1], "'changed friendly name to `") != NULL)
	{
		/* Pass it on as a server notice */
		cmd->source = server->name;

		/* Update the realname */
		/* :root!root@localhost PRIVMSG #bitlbee :User `jeroen' changed friendly name to `jeroen' */
		c = strstr(cmd->p[1], "' ");

		memset(tmp, 0, sizeof(tmp));
		strncpy(tmp, &cmd->p[1][6],
			((unsigned int)(c-cmd->p[1]))-6 > sizeof(tmp) ?
			sizeof(tmp) : (unsigned int)(c-cmd->p[1])-6);

		u = user_find_nick(tmp);
		if (u)
		{
			if (u->server != server)
			{
				dolog(LOG_WARNING, "server", "Received a friendly name change for %s who is not from %s:%s but from %s:%s\n",
					tmp, server->hostname, server->port, u->server->hostname, u->server->port);
				return;
			}
			c = strstr(c, " `");
			memset(tmp, 0, sizeof(tmp));
			strncpy(tmp, c+2,
				strlen(cmd->p[1])-3 > sizeof(tmp) ? sizeof(tmp) : strlen(cmd->p[1])-3);

			user_change_realname(u, tmp);
			return;
		}
		else
		{
			dolog(LOG_WARNING, "server", "Received friendly name change for non-existing user %s on %s!%s\n",
				tmp, server->hostname, server->port);
			return;
		}
	}

	/*
	 * ":root!root@localhost PRIVMSG #bitlbee :Question on MSN connection (handle chatbox@stargazers.nl):"
	 * ":root!root@localhost PRIVMSG #bitlbee :The user fuzzel@unfix.org (fuzzel@unfix.org) wants to add you to his/her buddy list. Do you want to allow this?"
	 * ":root!root@localhost PRIVMSG #bitlbee :You can use the yes/no commands to answer this question."
	 */
	if (g_conf->bitlbee_auto_add && 
		strncasecmp(cmd->p[1], "The user ", 9) == 0)
	{
		memset(tmp, 0, sizeof(tmp));
		c = strstr(cmd->p[1], " ( wants to add you");
		strncpy(tmp, &cmd->p[1][9],
			((unsigned int)(c-cmd->p[1]))-9 > sizeof(tmp) ?
			sizeof(tmp) : (unsigned int)(c-cmd->p[1])-9);
		/* This user wants to add me, thus add them too :) */
		server_printf(server, "PRIVMSG #bitlbee :add 0 %s\n", tmp);
	}

	if (strcasecmp(cmd->p[1], "You can use the yes/no commands to answer this question.") == 0)
	{
		/* Answer the question ;) */
		server_printf(server, "PRIVMSG #bitlbee :yes\n");
		return;
	}

	/* ":root!root@localhost PRIVMSG #bitlbee :MSN - Error: Error reported by MSN server: Internal server error/Account banned" */
	if (strcasecmp(cmd->p[1], "MSN - Error: Error reported by MSN server: Internal server error/Account banned") == 0)
	{
		/* Reconnect all the stupid accounts (I assume there will not be more than i ;) */
		for (i=0;i<4;i++)
		{
			server_printf(server, "PRIVMSG #bitlbee :account on %u\n", i);
		}
		return;
	}

#if 0
	dolog(LOG_DEBUG, "server", "Ignoring message from root\n");
#endif
	return;
}

/* A message from a BitlBee server that starts with a '!' */
void server_handle_bitlbee_command(struct server *server, struct irccmd *cmd)
{
	unsigned int		i;
	struct channeluser	*cu;
	struct channel		*ch;
	struct user		*u;
	struct listnode		*ln;

	if (strcasecmp(cmd->p[1], "!help") == 0)
	{
		server_printf(server,
			"PRIVMSG %s :#########################################\n"
			"PRIVMSG %s :### Talamasca (%s) Commands:\n"
			"PRIVMSG %s :### !nick <nick>   - Change nickname\n"
			"PRIVMSG %s :### !add           - Add yourself to this gateway, required if you want messages\n"
			"PRIVMSG %s :### !remove        - Remove yourself from the gateway, you won't get any messages anymore at all\n",
			cmd->source, cmd->source, TALAMASCA_VERSION,
			cmd->source, cmd->source, cmd->source, cmd->source);

		server_printf(server,
			"PRIVMSG %s :###\n"
			"PRIVMSG %s :### !names         - See who is on the channel\n"
			"PRIVMSG %s :### !topic         - See the current channel topic\n"
			"PRIVMSG %s :### !whoami        - Who am I?\n"
			"PRIVMSG %s :### !whois <nick>  - Query for information about a user\n",
			cmd->source, cmd->source, cmd->source, cmd->source, cmd->source);

		server_printf(server,
			"PRIVMSG %s :### !join          - Join the channel and see what people type\n"
			"PRIVMSG %s :### !part          - Part the channel until you log out, makes you completely invisible\n",
			cmd->source, cmd->source);

		server_printf(server,
			"PRIVMSG %s :###\n"
			"PRIVMSG %s :### !help          - This help\n"
			"PRIVMSG %s :### !admin         - Display Administrative information\n"
			"PRIVMSG %s :### !motd          - Display the Message Of The Day\n"
			"PRIVMSG %s :### !info          - Display Talamasca information\n"
			"PRIVMSG %s :### !version       - Display Talamasca version information\n"
			"PRIVMSG %s :### !uptime        - Display Talamasca uptime\n"
			"PRIVMSG %s :### !stats         - Display Talamasca statistics\n",
			cmd->source, cmd->source, cmd->source, cmd->source,
			cmd->source, cmd->source, cmd->source, cmd->source);

		server_printf(server,
			"PRIVMSG %s :#########################################\n",
			cmd->source);
		return;
	}

	if (strcasecmp(cmd->p[1], "!add") == 0)
	{
		for (i=0;i<4;i++)
		{
			server_printf(server, "PRIVMSG #bitlbee :add %u %s@%s\n", i, cmd->ident, cmd->host);
		}
		server_printf(server,
			":%s PRIVMSG %s :### Account addition completed, you might have to approve it in your client, use !remove to remove again\n",
			server->name, cmd->source);
		return;
	}

	if (strcasecmp(cmd->p[1], "!remove") == 0)
	{
		server_printf(server,
			"PRIVMSG %s :### Account removal completed, use !add to add again\n",
			cmd->source);
		server_printf(server,
			"PRIVMSG #bitlbee :remove %s\n",
			cmd->source);
		return;
	}
	
	if (strcasecmp(cmd->p[1], "!admin") == 0)
	{
		server_printf(server,
			"PRIVMSG %s :#######################################\n"
			"PRIVMSG %s :### %s's Administrative info\n"
			"PRIVMSG %s :### %s\n"
			"PRIVMSG %s :### %s\n"
			"PRIVMSG %s :### %s\n"
			"PRIVMSG %s :#######################################\n",
			cmd->source,
			cmd->source, g_conf->service_name,
			cmd->source, g_conf->admin_location1 ? g_conf->admin_location1 : "Not configured",
			cmd->source, g_conf->admin_location2 ? g_conf->admin_location2 : "Not configured",
			cmd->source, g_conf->admin_email ? g_conf->admin_email : "Not configured",
			cmd->source);
		return;
	}

	if (strcasecmp(cmd->p[1], "!stats") == 0)
	{
		struct server	*srv;
		struct listnode	*ln;

		server_printf(server,
			"PRIVMSG %s :#######################################\n"
			"PRIVMSG %s :### <server> <sendq> <sentmsg> <sentKB> <recvmsg> <recvKB> <connecttime>\n",
			cmd->source, cmd->source);

		LIST_LOOP(g_conf->servers, srv, ln)
		{
			server_printf(server,
				"PRIVMSG %s :### %s 0 %llu %llu %llu %llu %u\n",
				cmd->source,
				srv->identity,
				srv->stat_sent_msg,
				srv->stat_sent_bytes/1024,
				srv->stat_recv_msg,
				srv->stat_recv_bytes/1024,
				time(NULL) - srv->lastconnect);
		}

		server_printf(server,
			"PRIVMSG %s :#######################################\n",
			cmd->source);
		return;
	}

	if (strcasecmp(cmd->p[1], "!uptime") == 0)
	{
		unsigned int uptime_s = time(NULL) - g_conf->boottime, uptime_d, uptime_h, uptime_m;

		/* Offset the time (only per-second accuracy) */
		uptime_d  = uptime_s / (24*60*60);
		uptime_s -= uptime_d *  24*60*60;
		uptime_h  = uptime_s / (60*60);
		uptime_s -= uptime_h *  60*60;
		uptime_m  = uptime_s /  60;
		uptime_s -= uptime_m *  60;

		server_printf(server,
			"PRIVMSG %s :#######################################\n"
			"PRIVMSG %s :### Server Up %u days %u:%02u:%02u\n"
			"PRIVMSG %s :#######################################\n",
			cmd->source, cmd->source,
			uptime_d, uptime_h, uptime_m, uptime_s,
			cmd->source);
		return;
	}

	if (strcasecmp(cmd->p[1], "!motd") == 0)
	{
		FILE		*f = NULL;
		char		buf[1024];
		unsigned int	i;
	
		if (g_conf->motd_file) f = fopen(g_conf->motd_file, "r");
		if (!f)
		{
			server_printf(server,
				"PRIVMSG %s :### MOTD File is missing\n",
				cmd->source);
			return;
		}
	
		server_printf(server,
			"PRIVMSG %s :### %s Message of the day\n",
			cmd->source, server->name);
	
		while (fgets(buf, sizeof(buf), f))
		{
			i = strlen(buf);
			/* Trim off the newline*/
			if (buf[i-1] == '\n') buf[i-1] = '\0';
	
			server_printf(server,
				"PRIVMSG %s :### %s\n",
				cmd->source, buf);
		}
	
		server_printf(server,
			"PRIVMSG %s :### End of MOTD command\n",
			cmd->source);
	
		/* Close the file */
		fclose(f);
		return;
	}

	if (strcasecmp(cmd->p[1], "!info") == 0)
	{
		server_printf(server,
			"PRIVMSG %s :==--------------------------------==\n"
			"PRIVMSG %s :            The Talamasca\n"
			"PRIVMSG %s :\n"
			"PRIVMSG %s :        Linkers of the channels\n"
			"PRIVMSG %s :\n"
			"PRIVMSG %s :              We watch\n"
			"PRIVMSG %s :        And we are always here\n"
			"PRIVMSG %s :\n"
			"PRIVMSG %s :        GOUDA           ZURICH\n",
			cmd->source, cmd->source, cmd->source, cmd->source,
			cmd->source, cmd->source, cmd->source, cmd->source,
			cmd->source);
		/*
		 * If you have the intention of editing this message,
		 * then keep at least the Copyright notice in there.
		 * Some people simply want a little respect and credit.
		 */
		server_printf(server,
			"PRIVMSG %s :==--------------------------------==\n"
			"PRIVMSG %s :(C) Jeroen Massar <jeroen@unfix.org>\n"
			"PRIVMSG %s :==--------------------------------==\n"
			"PRIVMSG %s :http://unfix.org/projects/talamasca/\n"
			"PRIVMSG %s :==--------------------------------==\n"
			"PRIVMSG %s :End of INFO list\n",
			cmd->source, cmd->source, cmd->source,
			cmd->source, cmd->source, cmd->source);
		return;
	}

	if (strcasecmp(cmd->p[1], "!version") == 0)
	{
		/*
		 * If you have the intention of editing this message,
		 * then keep at least the Copyright notice in there.
		 * Some people simply want a little respect and credit.
		 */
		server_printf(server,
			"PRIVMSG %s : Talamasca %s (C) Copyright Jeroen Massar 2004 All Rights Reserved\n",
			cmd->source, TALAMASCA_VERSION);
		return;
	}

	if (	strcasecmp(cmd->p[1], "!join") == 0 ||
		 strcasecmp(cmd->p[1], "!part") == 0 ||
		 strcasecmp(cmd->p[1], "!names") == 0 ||
		 strcasecmp(cmd->p[1], "!topic") == 0)
	{
		if (!cmd->user)
		{
			server_printf(server,
				"PRIVMSG %s :### Please use !add first\n",
				cmd->source);
			return;
		}

		/* Our default channel */
		ch = server->defaultchannel;
		if (!ch)
		{
			server_printf(server,
				"PRIVMSG %s :### No default channel is configured\n",
				cmd->source);
			return;
		}

		if (strcasecmp(cmd->p[1], "!join") == 0)
		{
			if (ch)
			{
				if (channel_find_user(ch, cmd->user))
				{
					server_printf(server,
						"PRIVMSG %s :### You are already in the conversation\n",
						cmd->source);
					return;
				}
			}
			else
			{
				dolog(LOG_DEBUG, "server", "No default channel is configured\n");
				return;
			}
			
			if (!ch)
			{
				server_printf(server,
					"PRIVMSG %s :### Could not join you to the conversation\n",
					cmd->source);
				return;
			}

			server_printf(server,
				"PRIVMSG %s :### You have joined the conversation\n",
				cmd->source);

			channel_adduser(ch, cmd->user);
			return;
		}
		else if (strcasecmp(cmd->p[1], "!part") == 0)
		{
			if (!ch)
			{
				server_printf(server,
					"PRIVMSG %s :### Could not remove you from the conversation (no such channel)\n",
					cmd->source);
				return;
			}

			if (!channel_find_user(ch, cmd->user))
			{
				server_printf(server,
					"PRIVMSG %s :### You are not in the conversation\n",
					cmd->source);
				return;
			}

			server_printf(server,
				"PRIVMSG %s :### You have parted the conversation\n",
				cmd->source);
			channel_deluser(ch, cmd->user, "Parting the conversation", true);
			return;
		}
		else if (strcasecmp(cmd->p[1], "!topic") == 0)
		{
			struct tm	teem;
			char		tmp[128];

			if (!ch)
			{
				server_printf(server,
					"PRIVMSG %s :### Topic not available (no default channel)\n",
					cmd->source);
				return;
			}

			if (!channel_find_user(ch, cmd->user))
			{
				server_printf(server,
					"PRIVMSG %s :### If you want to see who is there, join first ;)\n",
					cmd->source);
				return;
			}

			/* Return the topic from the channel */
			if (!ch->topic)
			{
				server_printf(server,
					"PRIVMSG %s :### No Channel Topic has been set\n",
					cmd->source);
				return;
			}
			
			gmtime_r(&ch->topic_when, &teem);
			strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &teem);

			server_printf(server,
				"PRIVMSG %s :### Topic: \"%s\"\n"
				"PRIVMSG %s :### Set by %s at %s GMT\n",
				cmd->source, ch->topic,
				cmd->source, ch->topic_who, tmp);
			return;
		}
		else if (strcasecmp(cmd->p[1], "!names") == 0)
		{
			if (!ch)
			{
				server_printf(server,
					"PRIVMSG %s :### No names list available (no default channel)\n",
					cmd->source);
				return;
			}

			if (!channel_find_user(ch, cmd->user))
			{
				server_printf(server,
					"PRIVMSG %s :### If you want to see who is there, join first ;)\n",
					cmd->source);
				return;
			}

			server_printf(server,
				"PRIVMSG %s :###############################\n"
				"PRIVMSG %s :### Channel members:\n",
				cmd->source, cmd->source);

			LIST_LOOP(ch->users, cu, ln)
			{
				if (!cu->introduced) return;
				u = cu->user;

				server_printf(server,
					"PRIVMSG %s :### %s (%s@%s) - %s\n",
					cmd->source,
					u->nick, u->ident, u->host, u->realname);
			}

			server_printf(server,
				"PRIVMSG %s :################\n",
				cmd->source);
			return;
		}
	}

	if (strcasecmp(cmd->p[1], "!whoami") == 0)
	{
		server_printf(server,
			"PRIVMSG %s :### You are %s (%s@%s) - %s\n",
			cmd->source, cmd->source,
			cmd->ident, cmd->host,
			cmd->user->realname);
		return;
	}

	if (strncasecmp(cmd->p[1], "!whois ", 7) == 0)
	{
		/* Find the user */
		u = user_find_nick(&cmd->p[1][7]);
		if (!u)
		{
			server_printf(server,
				":%s PRIVMSG %s :### No such user '%s'\n",
				server->name, cmd->source, &cmd->p[1][7]);
			return;
		}

		server_printf(server,
			"PRIVMSG %s :###############################\n"
			"PRIVMSG %s :### Whois Information for %s\n"
			"PRIVMSG %s :### Realname    : %s\n",
			cmd->source,
			cmd->source, u->nick,
			cmd->source, u->realname);
		server_printf(server,
			"PRIVMSG %s :### Identity    : %s@%s\n",
			cmd->source, u->ident, u->host);
		LIST_LOOP(u->channels, ch, ln)
		{
			/* Find the user on the channel */
			cu = channel_find_user(ch, u);
			/* Should not happen but still.... */
			if (!cu)
			{
				dolog(LOG_WARNING, "server", "User %s!%s@%s is not really on %s\n",
					u->nick, u->ident, u->host, ch->name);
				continue;
			}
			server_printf(server,
				"PRIVMSG %s :### Channel     : %s%s%s @ %s\n",
				cmd->source,
				cu->f_operator	? "@" : "",
				cu->f_voice	? "+" : "",
				ch->name, ch->server->identity);
		}
		server_printf(server,
			"PRIVMSG %s :### Server      : %s [%s]\n",
			cmd->source, u->server->identity, u->server->description);
		if (u->away)
		{
			server_printf(server,
				"PRIVMSG %s :### Away Reason : %s\n",
				cmd->source, u->away);
		}
		i = time(NULL) - u->lastmessage;
		server_printf(server,
			"PRIVMSG %s :### Idle time   : %u second%s\n",
			cmd->source,
			i, i == 1 ? "" : "s");
		server_printf(server,
			"PRIVMSG %s :#################\n",
			cmd->source);
		return;
	}

	if (strncasecmp(cmd->p[1], "!nick ", 6) == 0)
	{
		u = user_find_nick(&cmd->p[1][6]);
		if (u)
		{
			server_printf(server,
				"PRIVMSG %s :### Someone else is already using that nick\n",
				cmd->source);
			return;
		}

		/* Nicks must start with A-Z or a-z */
		if (!is_nickokay(&cmd->p[1][6]))
		{
			server_printf(server,
				"PRIVMSG %s :### Nicknames must start with an alphabetical character\n",
				cmd->source);
			return;
		}

		server_printf(server,
			"PRIVMSG %s :### Changing your name from %s to %s\n",
			cmd->source, cmd->source, &cmd->p[1][6]);

		/*
		 * Ask BitlBee to rename which will issue a NICK after it
		 * Let's hope this is fast enough that the other server
		 * doesn't change a nick otherwise we have a collide
		 * in that case we simply rename the user to something else ;)
		 */
		server_printf(server,
			"PRIVMSG #bitlbee :rename %s %s\n",
			cmd->source, &cmd->p[1][6]);
		return;
	}

	/* Command unknown */
	server_printf(server,
		"PRIVMSG %s :### Unknown command, see !help\n",
		cmd->source);
	return;
}

/* p0=chan, p1=msg */
void server_handle_privmsg(struct server *server, struct irccmd *cmd)
{
	char			*c = NULL, tmp[1024];
	struct user		*u = NULL;
	struct channel		*ch = NULL;
	struct channeluser	*cu = NULL;
	struct server		*srv = NULL;
	struct listnode		*ln = NULL;
	unsigned int		i;
	bool			relay = true;

	/* When it is a BitlBee */
	if (server->type == SRV_BITLBEE)
	{
		/* Message from 'root', usually to #bitlbee ? */
		if (strcasecmp(cmd->source, "root") == 0)
		{
			server_handle_bitlbee_root(server, cmd);
			return;
		}

		if (cmd->p[1][0] == '!')
		{
			server_handle_bitlbee_command(server, cmd);
			return;
		}
	}

	/* No user for this source when it is a BitlBee server and not a command? */
	if (	server->type == SRV_BITLBEE &&
		!cmd->user)
	{
		server_printf(server,
			"PRIVMSG %s :### You are currently not active, use !add first, also see !help\n",
			cmd->source);
		return;
	}

	/* Check for faulty nicknames */
	if (	server->type == SRV_BITLBEE &&
		!is_nickokay(cmd->source))
	{
		char tmp[20];

		/* Change it */
		if (!getfreenick(tmp, sizeof(tmp))) return;
		server_printf(server, "PRIVMSG #bitlbee :rename %s %s\n", cmd->source, tmp);
		return;
	}

	if (!cmd->user)
	{
		/* This can happen with a userlink server and out-of-channel-messages */
		dolog(LOG_WARNING, "Received a message from %s!%s@%s who doesn't exist... requesting information\n",
			cmd->source, cmd->ident, cmd->host);

		/* Let's find out information about this person */
		server_printf(server, "WHOIS %s\n", cmd->source);
		return;
	}

	/* Reset the idle timer */
	user_resetidle(cmd->user);

	if (	server->type == SRV_USER ||
		server->type == SRV_BITLBEE)
	{
		/* Check for private messages */
		c = strstr(cmd->p[1], ": ");
		if (c)
		{
			memset(tmp, 0, sizeof(tmp));
			strncpy(tmp, cmd->p[1],
				((unsigned int)(c-cmd->p[1])) > sizeof(tmp) ?
				sizeof(tmp) : (unsigned int)(c-cmd->p[1]));
			/* Check for spaces in the nick, might be that somebody types something like: "Example Format: ..." */
			if (!strstr(tmp, " "))
			{
				u = user_find_nick(tmp);
				if (!u)
				{
					dolog(LOG_DEBUG, "privmsg", "Couldn't find target user %s\n", tmp);
					server_printf(server, "PRIVMSG %s :No such nick/channel %s\n",
						cmd->source, tmp);
					return;
				}

				dolog(LOG_DEBUG, "privmsg", "Treating it as a private message from %s to %s\n", cmd->source, tmp);
				relay = false;
				/* Cut off the prefix */
				cmd->p[1] = c+2;
			}
		}
	}
	else relay = false;

	/*
	 * - Message to a channel?
	 * - Or a BitlBee Relay ?
	 */
	if (	cmd->p[0][0] == '#' ||
		cmd->p[0][0] == '!' ||
		cmd->p[0][0] == '&' ||
		cmd->p[0][0] == '$' ||
		relay)
	{
		/* Use the default channel? */
		if (relay) ch = server->defaultchannel;
		/* Find the channel based on the name */
		else ch = server_find_channel(server, cmd->p[0]);
		if (!ch)
		{
			if (relay)
			{
				dolog(LOG_DEBUG, "privmsg", "No default channel on server %s:%s\n",
					server->hostname, server->port);
				return;
			}

			dolog(LOG_DEBUG, "privmsg", "Couldn't find channel %s on %s:%s\n",
				cmd->p[0], server->hostname, server->port);

			if (	server->type == SRV_RFC1459 ||
				server->type == SRV_TS)
			{
				server_printf(server,
					":%s 403 %s %s :No such nick/channel\n",
					server->name, cmd->source, cmd->p[0]);
			}
			else if (server->type == SRV_P10)
			{
				dolog(LOG_DEBUG, "privmsg", "P10 is not implemented\n");
				exit(-42);
			}
			else if (server->type == SRV_BITLBEE ||
				 server->type == SRV_BITLBEE)
			{
				server_printf(server,
					"PRIVMSG %s :No such nick/channel %s\n",
					cmd->source, cmd->p[0]);
			}
			return;
		}

		if (!ch->link)
		{
			dolog(LOG_DEBUG, "privmsg", "Channel is not linked!?\n");
			return;
		}

		/* Verify that the sender is on the channel */
		if (!channel_find_user(ch->link, cmd->user))
		{
			if (	server->type == SRV_RFC1459 ||
				server->type == SRV_TS)
			{
				server_printf(server,
					":%s 442 %s %s :You're not on that channel\n",
					server->name, cmd->source, cmd->p[0]);
			}
			else if (server->type == SRV_P10)
			{
				dolog(LOG_DEBUG, "privmsg", "P10 is not implemented\n");
				exit(-42);
			}
			else if (server->type == SRV_BITLBEE ||
				 server->type == SRV_BITLBEE)
			{
				server_printf(server,
					"PRIVMSG %s :You should join the channel first\n",
					cmd->source);
			}
			return;
		}

		/* Do I need to bounce this back? */
		if (relay && server->type == SRV_BITLBEE)
		{
			channel_message(ch, cmd->user, cmd->p[1]);
		}
		
		/* Relay the message to the linked channel */
		channel_message(ch->link, cmd->user, cmd->p[1]);
		return;
	}

	dolog(LOG_DEBUG, "privmsg", "Going for User to user messaging\n");

	/* Figure out the target user (bitlbee+user already done above) */
	if (	server->type == SRV_RFC1459 ||
		server->type == SRV_TS)
	{
		/* Find the nick from the target */
		u = user_find_nick(cmd->p[0]);
		if (!u)
		{
			server_printf(server,
				":%s 401 %s %s :No such nick/channel\n",
				server->name, cmd->source, cmd->p[0]);
			return;
		}
	}
	if (server->type == SRV_P10)
	{
		dolog(LOG_ERR, "privmsg", "P10 is not implemented\n");
		exit(-42);
	}

	/* Did we find a user to relay this too? */
	if (!u)
	{
		dolog(LOG_DEBUG, "privmsg", "Couldn't find nick %s for relaying - should not happen\n", cmd->p[0]);
		exit(-666);
	}

	/* Relay the message to the target user */
	if (	u->server->type == SRV_RFC1459 ||
		u->server->type == SRV_TS)
	{
		server_printf(u->server,
			":%s PRIVMSG %s :%s\n",
			cmd->source, u->nick, cmd->p[1]);
		return;
	}

	if (u->server->type == SRV_P10)
	{
		dolog(LOG_ERR, "privmsg", "P10 is not implemented\n");
		exit(-42);
	}

	/* User link */
	server_printf(u->server,
		"PRIVMSG %s :[%s] %s\n",
		u->nick, cmd->source, cmd->p[1]);
}

/* source=who, p[0] = channel */
void server_handle_join(struct server *server, struct irccmd *cmd)
{
	struct channel	*ch = NULL;

	if (!cmd->user)
	{
		/* Ignore nick adds for the 'root' user of bitlbee */
		if (	server->type == SRV_BITLBEE &&
			strcasecmp(cmd->source, "root") == 0)
		{
			dolog(LOG_DEBUG, "server", "Ignoring Delayed user_add() for root on a BitlBee server\n");
			return;
		}

		dolog(LOG_DEBUG, "server", "Delay adding user %s because of JOIN to %s\n", cmd->source, cmd->p[0]);
		server_printf(server, "WHOIS %s\n", cmd->source);
		return;
	}

	if (
		/* BitlBee Server? */
		(server->type == SRV_BITLBEE) &&
		/* Is this about me? */
		cmd->user == server->user &&
		/* and about #bitlbee ? :) */
		strcasecmp(cmd->p[0], "#bitlbee") == 0)
	{
		/* Identify us to the big bee */
		server_printf(server, "PRIVMSG #bitlbee :identify %s\n",
			server->bitlbee_identifypass);
		return;
	}

	ch = server_find_channel(server, cmd->p[0]);
	
	/* Not found? -> Create it */
	if (!ch) ch = channel_add(server, cmd->p[0], NULL);

	/* Don't join twice ;) */
	if (channel_find_user(ch, cmd->user)) return;

	/* Add the user */	
	channel_adduser(ch, cmd->user);
}

/* source=who, p[0] = channel (or multiple!) */
void server_handle_part(struct server *server, struct irccmd *cmd)
{
	struct channel *ch;

	if (!cmd->user)
	{
		dolog(LOG_DEBUG, "server", "Received a part for unknown user %s on channel %s\n", cmd->source, cmd->p[0]);
	}
	else if (
		/* BitlBee server? */
		server->type == SRV_BITLBEE &&
		/* Is this about me? */
		cmd->user && cmd->user->server == NULL &&
		/* and about #bitlbee ? :) */
		strcasecmp(cmd->p[0], "#bitlbee") == 0)
	{
		/* Should not happen IMHO :) */
	}
	else
	{
		ch = server_find_channel(server, cmd->p[0]);
		if (ch) channel_deluser(ch, cmd->user, "Leaving...", true);
	}
}

void server_handle_quit(struct server *server, struct irccmd *cmd)
{
	if (!cmd->user)
	{
		dolog(LOG_DEBUG, "server", "Received a part for unknown user %s, reason: %s\n", cmd->source, cmd->p[0]);
		return;
	}
	user_destroy(cmd->user, "Server quit");
}

/* source=who, p0=channel[,channel] p1=who[,who,who] p2=reason */
void server_handle_kick(struct server *server, struct irccmd *cmd)
{
	struct channel	*ch;
	struct user	*user;
	char		reason[256];
	
	snprintf(reason, sizeof(reason), "Kicked by %s: %s", cmd->source, cmd->p[2]);

	ch = server_find_channel(server, cmd->p[0]);
	if (!ch)
	{
		dolog(LOG_DEBUG, "server", "Could not find channel %s where %s got kicked by %s with reason %s\n",
			cmd->p[0], cmd->p[1], cmd->source, cmd->p[2]);
		return;
	}
	user = user_find_nick(cmd->p[1]);
	if (!user)
	{
		dolog(LOG_DEBUG, "server", "Could not find user %s who got kicked of %s by %s with reason %s\n",
			cmd->p[1], cmd->p[0], cmd->source, cmd->p[2]);
		return;
	}

	/* Delete the user from the channel */
	channel_deluser(ch, user, reason, false);

	/* Notify the Bitlbee user */
	if (user->server->type == SRV_BITLBEE)
	{
		/* Notify the user of this action */
		server_printf(user->server,
			":%s PRIVMSG %s :### You have been kicked from %s by %s (%s)\n",
			user->server->name,
			user->nick,
			cmd->p[0], cmd->source, cmd->p[2]);
	}
}

/*
 * p0 = channel, p1 = mode(s), p2-pX=users
 * p0 = user, p1 = mode(s)
 */
void server_handle_mode(struct server *server, struct irccmd *cmd)
{
	/* Channel mode ? */
	if (	cmd->p[0][0] == '#' ||
		cmd->p[0][0] == '!' ||
		cmd->p[0][0] == '&' ||
		cmd->p[0][0] == '$')
	{
		char			*mode = cmd->p[1];
		unsigned int		off = 2;
		bool			sign = true;
		struct channel		*ch = NULL;
		struct channeluser	*cu = NULL;
		struct user		*u = NULL;

		ch = server_find_channel(server, cmd->p[0]);
		if (!ch)
		{
			dolog(LOG_DEBUG, "server", "Received mode change for unknown channel %s on %s:%s\n",
				cmd->p[0], server->hostname, server->port);
			return;
		}

		/* Parse all the mode's */
		for (mode=cmd->p[1]; *mode != '\0'; mode++)
		{
			/* Sign ?*/
			if (*mode == '+')
			{
				sign = true;
				continue;
			}
			if (*mode == '-')
			{
				sign = false;
				continue;
			}
			
			if (	*mode == 'O' ||
				*mode == 'o' ||
				*mode == 'v')
			{
				if (cmd->numargs <= off)
				{
					dolog(LOG_ERR, "server", "Received a %c mode change on channel %s but without a user\n",
						*mode, ch->name);
					return;
				}
				u = user_find_nick(cmd->p[off]);
				if (!u)
				{
					dolog(LOG_ERR, "server", "Received a %c mode change on channel %s for %s who does not exist\n",
						*mode, ch->name, cmd->p[off]);
					return;
				}
				cu = channel_find_user(ch, u);
				if (!cu)
				{
					dolog(LOG_ERR, "server", "Received a %c mode change on channel %s for %s who is not on that channel\n",
						*mode, ch->name, cmd->p[off]);
					return;
				}
				off++;
			}

			/* See RFC 2811 page 7 */
			switch (*mode)
			{
				case 'O':	/* O - give "channel creator" status; */
					cu->f_creator = sign;
					break;

				case 'o':	/* o - give/take channel operator privilege; */
					cu->f_operator = sign;
					break;

				case 'v':	/* v - give/take the voice privilege; */
					cu->f_voice = sign;

					/* On BitlBee's also update the away information */
					if (server->type != SRV_BITLBEE) break;
					if (sign)
					{
						/* The user is back */
						user_change_away(cu->user, NULL);
					}
					else
					{
						/*
						 * Request information about this user
						 * This contains the away message
						 */
						server_printf(server, "WHOIS %s\n", cu->user->nick);
					}
					break;

				case 'a':	/* a - toggle the anonymous channel flag; */
					ch->f_anonymous = sign;
					break;
				case 'i':	/* i - toggle the invite-only channel flag; */
					ch->f_invite = sign;
					break;
				case 'm':	/* m - toggle the moderated channel; */
					ch->f_moderated = sign;
					break;
				case 'n':	/* n - toggle the no messages to channel from clients on the outside; */
					ch->f_nooutside = sign;
					break;
				case 'q':	/* q - toggle the quiet channel flag; */
					ch->f_moderated = sign;
					break;
				case 'p':	/* p - toggle the private channel flag; */
					ch->f_private = sign;
					break;
				case 's':	/* s - toggle the secret channel flag; */
					ch->f_secret = sign;
					break;
				case 'r':	/* r - toggle the server reop channel flag; */
					ch->f_reop = sign;
					break;
				case 't':	/* t - toggle the topic settable by channel operator only flag; */
					ch->f_topiclock = sign;
					break;

				case 'k':	/* k - set/remove the channel key (password); */
					channel_change_key(ch, cmd->p[off]);
					off++;
					break;

				case 'l':	/* l - set/remove the user limit to channel; */
					if (sign)
					{
						ch->limit = atoi(cmd->p[off]);
						off++;
					}
					else ch->limit = -1;
					break;

				case 'b':	/* b - set/remove ban mask to keep users out; */
					/* TODO: Implement banning */
					break;
				case 'e':	/* e - set/remove an exception mask to override a ban mask; */
					/* TODO: Implement exceptions */
					break;
				case 'I':	/* I - set/remove an invitation mask to automatically override the invite-only flag; */
					/* TODO: Implement invitations */
					break;

				default:
					dolog(LOG_DEBUG, "server", "Received Unknown Channel Mode flag %c for %s on %s:%s\n",
						*mode, ch->name, server->hostname, server->port);
					break;
			}
		}
		return;
	}

}

void server_handle_away(struct server *server, struct irccmd *cmd)
{
	/* Away changes */
	if (!cmd->user)
	{
		dolog(LOG_DEBUG, "server", "Received away from unknown user '%s' (%p)\n",
			cmd->source, cmd->user);
		return;
	}

	user_change_away(cmd->user, cmd->p[0]);

	/* Broadcast away to BitlBee users */
}

void server_handle_nick(struct server *server, struct irccmd *cmd)
{
	struct user	*u;

	/* Ignore nick adds for the 'root' user of bitlbee */
	if (	server->type == SRV_BITLBEE &&
		strcasecmp(cmd->p[0], "root") == 0)
	{
		dolog(LOG_DEBUG, "server", "Ignoring user_add() for root on a BitlBee server\n");
		return;
	}

	/* Server Join */
	if (cmd->p[8] != NULL)
	{
		u = user_find_nick(cmd->p[0]);
		if (!u)
		{
			/* Didn't exist yet */
			u = user_add(cmd->p[0], server, false);
			user_change_ident(u, cmd->p[4]);
			user_change_host(u, cmd->p[5]);
			user_change_realname(u, cmd->p[8]);
			user_introduce(u);
		}
		else
		{
			/* Already exists -> Collision case */
			dolog(LOG_ERR, "server", "Collision for nick %s\n", cmd->p[0]);
			server_printf(server, ":%s KILL %s :That nickname is reserved, pick another one (SJ)\n",
				server->name, cmd->p[0]);
			return;
		}
	}
	/* Nick change */
	else
	{
		u = user_find_nick(cmd->p[0]);
		if (u)
		{
			dolog(LOG_WARNING, "server", "Received a nick change while nick is already in use!\n");

			if (	server->type == SRV_RFC1459 ||
				server->type == SRV_TS)
			{
				/* Collide */
				server_printf(server, ":%s KILL %s :That nickname is reserved, pick another one (NC)\n",
					server->name, cmd->p[0]);

				/* We collided, thus remove the user's previous self */
				if (cmd->user) user_destroy(cmd->user, "Collision");

			}
			else if (server->type == SRV_P10)
			{
				dolog(LOG_DEBUG, "server", "Not implemented\n");
				exit(-42);
			}
			else
			{
				/* Can't collide, thus just ignore the user */
			}
			return;
		}

		/* Change the nick */
		u = user_find_nick(cmd->source);
		if (u)
		{
			user_change_nick(u, cmd->p[0], false);
		}
		else
		{
			dolog(LOG_WARNING, "server", "Unknown user %s changed name to %s, asking for information\n",
				cmd->source, cmd->p[0]);
			server_printf(server, "WHOIS %s\n", cmd->p[0]);
		}
	}
}

void server_handle_whois(struct server *server, struct irccmd *cmd)
{
	struct user		*u;
	struct channel		*ch;
	struct listnode		*ln;
	struct channeluser	*cu;
	char			*nick;
	unsigned int		i;
	
	if (cmd->numargs == 2) nick = cmd->p[1];
	else nick = cmd->p[0];

	u = user_find_nick(nick);
	if (!u)
	{
		server_printf(server,
			":%s 401 %s %s :No such nick/channel\n",
			server->name, cmd->source, nick);
		dolog(LOG_WARNING, "server", "Unknown user %s during whois from %s\n",
			nick, cmd->source);
		return;
	}
	server_printf(server,
		":%s 311 %s %s %s %s * :%s\n",
		server->name, cmd->source, u->nick,
		u->ident, u->host, u->realname);
	LIST_LOOP(u->channels, ch, ln)
	{
		/* Only show channels on the same server */
		if (ch->server != server) continue;
		cu = channel_find_user(ch, u);
		/* User not in channel but on list!? */
		if (!cu)
		{
			dolog(LOG_WARNING, "server", "User %s!%s@%s is not really on %s\n",
				u->nick, u->ident, u->host, ch->name);
			continue;
		}
		server_printf(server,
			":%s 319 %s %s :%s%s%s\n",
			server->name, cmd->source, u->nick,
			cu->f_operator	? "@" : "",
			cu->f_voice	? "+" : "",
			ch->name);
	}
	server_printf(server,
		":%s 312 %s %s %s :%s\n",
		server->name, cmd->source, u->nick,
		u->server->identity, u->server->description);
	if (u->away)
	{
		server_printf(server,
			":%s 301 %s %s :%s\n",
			server->name, cmd->source, u->nick, u->away);
	}
	i = time(NULL) - u->lastmessage;
	server_printf(server,
		":%s 317 %s %s %u :second%s idle\n",
		server->name, cmd->source, u->nick,
		i, i == 1 ? "" : "s");
	server_printf(server,
		":%s 318 %s %s :End of WHOIS list\n",
		server->name, cmd->source, u->nick);
}

/* p0=channel, p1=who, p2=when, p3=topic */
void server_handle_topic(struct server *server, struct irccmd *cmd)
{
	struct channel *ch;

	ch = server_find_channel(server, cmd->p[0]);
	if (!ch)
	{
		dolog(LOG_DEBUG, "server", "Couldn't change topic for unknown channel %s\n", cmd->p[0]);
		return;
	}

	channel_change_topic(ch, cmd->p[3]);
	channel_change_topic_who(ch, cmd->p[1]);
	channel_change_topic_when(ch, atoi(cmd->p[2]));

	/* No link? -> Don't act then ;) */
	if (!ch->link) return;

	/* Forward it to the userlinks */
	channel_message(ch->link, cmd->user, "### %s set the topic to: %s\n",
		cmd->source, cmd->p[3]);
}

/* p0=who, p1=channel, p2=topic */
void server_handle_topic_332(struct server *server, struct irccmd *cmd)
{
	struct channel *ch;

	ch = server_find_channel(server, cmd->p[1]);
	if (!ch)
	{
		dolog(LOG_DEBUG, "server", "Couldn't change topic for unknown channel %s\n", cmd->p[1]);
		return;
	}

	channel_change_topic(ch, cmd->p[2]);
	channel_change_topic_who(ch, cmd->p[0]);
}

void server_handle_topic_333(struct server *server, struct irccmd *cmd)
{
}

void server_handle_version(struct server *server, struct irccmd *cmd)
{
	/*
	 * If you have the intention of editing this message,
	 * then keep at least the Copyright notice in there.
	 * Some people simply want a little respect and credit.
	 */
	server_printf(server,
		":%s 351 %s talamasca-%s :(C) Copyright Jeroen Massar 2004 All Rights Reserved\n",
		server->name, cmd->source,
		TALAMASCA_VERSION);
}

void server_handle_info(struct server *server, struct irccmd *cmd)
{
	server_printf(server,
		":%s 371 %s :==--------------------------------==\n"
		":%s 371 %s :            The Talamasca\n"
		":%s 371 %s :\n"
		":%s 371 %s :        Linkers of the channels\n"
		":%s 371 %s :\n"
		":%s 371 %s :              We watch\n"
		":%s 371 %s :        And we are always here\n"
		":%s 371 %s :\n"
		":%s 371 %s :        GOUDA           ZURICH\n",
		server->name, cmd->source,
		server->name, cmd->source,
		server->name, cmd->source,
		server->name, cmd->source,
		server->name, cmd->source,
		server->name, cmd->source,
		server->name, cmd->source,
		server->name, cmd->source,
		server->name, cmd->source);
	/*
	 * If you have the intention of editing this message,
	 * then keep at least the Copyright notice in there.
	 * Some people simply want a little respect and credit.
	 */
	server_printf(server,
		":%s 371 %s :==--------------------------------==\n"
		":%s 371 %s :(C) Jeroen Massar <jeroen@unfix.org>\n"
		":%s 371 %s :==--------------------------------==\n"
		":%s 371 %s :http://unfix.org/projects/talamasca/\n"
		":%s 371 %s :==--------------------------------==\n"
		":%s 374 %s :End of INFO list\n",
		server->name, cmd->source,
		server->name, cmd->source,
		server->name, cmd->source,
		server->name, cmd->source,
		server->name, cmd->source,
		server->name, cmd->source);
}

void server_handle_motd(struct server *server, struct irccmd *cmd)
{
	FILE		*f = NULL;
	char		buf[1024];
	unsigned int	i;

	if (g_conf->motd_file) f = fopen(g_conf->motd_file, "r");
	if (!f)
	{
		server_printf(server,
			":%s 422 %s :MOTD File is missing\n",
			server->name, cmd->source);
		return;
	}

	server_printf(server,
		":%s 375 %s :- %s Message of the day - \n",
		server->name, cmd->source, server->name);

	while (fgets(buf, sizeof(buf), f))
	{
		i = strlen(buf);
		/* Trim off the newline*/
		if (buf[i-1] == '\n') buf[i-1] = '\0';

		server_printf(server,
			":%s 372 %s :%s\n",
			server->name, cmd->source, buf);
	}

	server_printf(server,
		":%s 376 %s :End of MOTD command\n",
		server->name, cmd->source);

	/* Close the file */
	fclose(f);
}

void server_handle_admin(struct server *server, struct irccmd *cmd)
{
	server_printf(server,
		":%s 256 %s %s :Administrative info\n"
		":%s 257 %s :%s\n"
		":%s 258 %s :%s\n"
		":%s 259 %s :%s\n",
		server->name, cmd->source, server->name,
		server->name, cmd->source, g_conf->admin_location1 ? g_conf->admin_location1 : "Not configured",
		server->name, cmd->source, g_conf->admin_location2 ? g_conf->admin_location2 : "Not configured",
		server->name, cmd->source, g_conf->admin_email ? g_conf->admin_email : "Not configured");
}

void server_handle_time(struct server *server, struct irccmd *cmd)
{
	char buf[100];
	time_t tee = time(NULL);
	struct tm teem;

	gmtime_r(&tee, &teem);
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &teem);

	server_printf(server,
		":%s 391 %s :%s\n",
		server->name, cmd->source, buf);
}

/* p0 = what */
void server_handle_stats(struct server *server, struct irccmd *cmd)
{
	char buf[100];

	if (strcmp(cmd->p[0], "l") == 0)
	{
		struct server	*srv;
		struct listnode	*ln;

		LIST_LOOP(g_conf->servers, srv, ln)
		{
			server_printf(server,
				":%s 211 %s %s 0 %llu %llu %llu %llu %u\n",
				server->name, cmd->source,
				srv->identity,
				srv->stat_sent_msg,
				srv->stat_sent_bytes/1024,
				srv->stat_recv_msg,
				srv->stat_recv_bytes/1024,
				time(NULL) - srv->lastconnect);
		}
	}
	else if (strcmp(cmd->p[0], "u") == 0)
	{
		unsigned int uptime_s = time(NULL) - g_conf->boottime, uptime_d, uptime_h, uptime_m;
	
		/* Offset the time (only per-second accuracy) */
		uptime_d  = uptime_s / (24*60*60);
		uptime_s -= uptime_d *  24*60*60;
		uptime_h  = uptime_s / (60*60);
		uptime_s -= uptime_h *  60*60;
		uptime_m  = uptime_s /  60;
		uptime_s -= uptime_m *  60;
	
		server_printf(server,
			":%s 242 %s :Server Up %u days %u:%02u:%02u\n",
			server->name, cmd->source,
			uptime_d, uptime_h, uptime_m, uptime_s);
	}

	server_printf(server,
		":%s 219 %s %s :End of STATS report\n",
		server->name, cmd->source, cmd->p[0]);
}

void server_handle_connected(struct server *server, struct irccmd *cmd)
{
	struct user	*u;
	struct channel	*ch;
	struct listnode	*ln, *ln2;

	/* Welcome, we are connected */
	server->state = SS_CONNECTED;

	dolog(LOG_DEBUG, "server", "%s:%s is now in state: connected\n", server->hostname, server->port);

	/* Introduce our users */
	LIST_LOOP(g_conf->users, u, ln)
	{
		server_introduce(server, u);
	}	

	/* Introduce our channels */
	LIST_LOOP(server->channels, ch, ln)
	{
		/* Add all the channel users */
		LIST_LOOP(g_conf->users, u, ln2)
		{
			/* Don't join twice though */
			if (channel_find_user(ch, u)) continue;
			
			/* Add the user to the channel */
			channel_adduser(ch, u);
		}
	}
}

/* source = none, p0 = hostname/identity, p1 = hops, p2 = description */
void server_handle_server(struct server *server, struct irccmd *cmd)
{
	/* Not connected yet -> then mark this as connected */
	if (server->state != SS_CONNECTED) server_handle_connected(server, cmd);
	
	/* Record the information about this server */
	server_change_identity(server, cmd->p[0]);
	server_change_description(server, cmd->p[2]);
}

void server_handle_sjoin(struct server *server, struct irccmd *cmd)
{
	struct user	*u;
	struct channel	*ch;
	char		*c, nick[1024];
	unsigned int	i,j,k;

	/* 0/1=timestamps, 2 = name, 3 = chanmode, 4/5 = multiple users + modes */
	ch = server_find_channel(server, cmd->p[2]);
	if (!ch)
	{
		/* Create the channel */
		ch = channel_add(server, cmd->p[2], NULL);
	}
	if (strlen(cmd->p[4]) == 0) c = cmd->p[5];
	else c = cmd->p[4];
	i = countfields(c);
	for (j = 1; j <= i; j++)
	{
		copyfield(c, j, nick, sizeof(nick));
		if (	nick[0] == '@' ||			/* Channel Operator */
			nick[0] == '+')				/* Voiced user */
		{
			k = 1;
		}
		else if ((nick[0] == '@' && nick[1] == '@') ||	/* Channel Creator */
			 (nick[0] == '@' && nick[1] == '+'))	/* Ops + Voice */
		{
			k = 2;
		}
		else k = 0;
		u = user_find_nick(&nick[k]);
		if (u)
		{
			/* Don't add twice */
			if (channel_find_user(ch, u)) continue;
			channel_adduser(ch, u);
		}
		else
		{
			dolog(LOG_ERR, "server", "Unknown user %s for channel %s\n", nick, cmd->p[2]);
		}
	}
}

void server_handle_whois_353(struct server *server, struct irccmd *cmd)
{
	struct user	*u;
	struct channel	*ch;
	unsigned	i,j,k;
	char		nick[1024];

	/* 0 = nick, 1 = mode, 2 = chan, 3 = who */
	ch = server_find_channel(server, cmd->p[2]);
	if (!ch)
	{
		/* Create the channel */
		ch = channel_add(server, cmd->p[2], NULL);
	}
	i = countfields(cmd->p[3]);
	for (j = 1; j <= i; j++)
	{
		copyfield(cmd->p[3], j, nick, sizeof(nick));
		if (	nick[0] == '@' ||
			nick[0] == '+') k = 1;
		else k = 0;
		u = user_find_nick(&nick[k]);
		if (u)
		{
			if (u->server == server)
			{
				if (channel_find_user(ch, u)) continue;
				channel_adduser(ch, u);
			}
			else
			{
				dolog(LOG_DEBUG, "server", "User %s is not really on server %s:%s\n", u->nick, server->hostname, server->port);
			}
		}
		else
		{
			/* Ignore nick adds for the 'root' user of bitlbee */
			if (	server->type == SRV_BITLBEE &&
				strcasecmp(&nick[k], "root") == 0)
			{
				dolog(LOG_DEBUG, "server", "Ignoring Delayed user add for root on a BitlBee server\n");
				continue;
			}

			dolog(LOG_DEBUG, "server", "Delay adding user %s caused by 353\n", &nick[k], cmd->p[2]);
			server_printf(server, "WHOIS %s\n", &nick[k]);
		}
	}
}

void server_handle_whois_311(struct server *server, struct irccmd *cmd)
{
	struct user	*u;
	struct channel	*ch;
	
	/* We only want these on user links */
	if (	server->type != SRV_BITLBEE &&
		server->type != SRV_USER)
	{
		dolog(LOG_DEBUG, "server", "Received 311 reply on a server link\n");
		return;
	}

	/* 0=/me, 1=nick, 2=ident, 3=host, 4=server, 5=realname */
	u = user_find_nick(cmd->p[1]);
	
	/* Somebody else ? */
	if (u && server != u->server)
	{
		char tmp[20];

		/* Already exists -> Collision case */
		dolog(LOG_ERR, "server", "Collision for nick %s\n", cmd->p[1]);
		
		/* Can't do anything on normal user links */
		if (server->type != SRV_BITLBEE) return;

		if (!getfreenick(tmp, sizeof(tmp))) return;

		/* On BitlBee try to rename the user to something else */
		server_printf(server, "PRIVMSG #bitlbee :rename %s %s\n",
			cmd->p[1], tmp);
		return;
	}

	/* Already exists */
	if (u)
	{
		/* Update only */
		user_change_ident(u, cmd->p[2]);
		user_change_host(u, cmd->p[3]);
		user_change_realname(u, cmd->p[5]);
		return;
	}

	/* Didn't exist yet */
	u = user_add(cmd->p[1], server, false);
	if (!u)
	{
		dolog(LOG_WARNING, "server", "User addition failed!?\n");
		return;
	}
	user_change_ident(u, cmd->p[2]);
	user_change_host(u, cmd->p[3]);
	user_change_realname(u, cmd->p[5]);
	user_introduce(u);

	/*
	 * Bitlbee doesn't report the channels
	 * a user is on using a 319, thus fake it
	 */
	if (server->type != SRV_BITLBEE) return;

	ch = server_find_channel(server, "#bitlbee");
	if (!ch)
	{
		dolog(LOG_WARNING, "server", "No #bitlbee channel on a BitlBee linked server!?\n");
		return;
	}

	/* Don't add the user twice to the same channel */
	if (channel_find_user(ch, u)) return;

	/* Add the user to the local channel */
	channel_adduser(ch, u);

	/* Welcome the BitlBee user */
	welcome_bitlbee_user(u);
}

void server_handle_whois_319(struct server *server, struct irccmd *cmd)
{
	struct channel	*ch;
	struct user	*u;
	unsigned int	i,j;
	char		channame[1024];

	/* We only want these on user links */
	if (	server->type != SRV_BITLBEE &&
		server->type != SRV_USER)
	{
		dolog(LOG_DEBUG, "server", "Received 319 reply on a server link\n");
		return;
	}

	/*
	 * 319's are a result of a whois and we should have the user info already ;)
	 * 0=/me, 1=nick, 2=channels
	 */
	u = user_find_nick(cmd->p[1]);
	if (!u)
	{
		dolog(LOG_WARNING, "server", "Unknown user %s when receiving a 319\n", cmd->p[1]);
		return;
	}

	i = countfields(cmd->p[2]);
	for (j = 1; j <= i; j++)
	{
		copyfield(cmd->p[2], j, channame, sizeof(channame));
		ch = server_find_channel(server, channame);
		if (ch)
		{
			if (channel_find_user(ch, cmd->user)) continue;
			channel_adduser(ch, cmd->user);
		}
		else dolog(LOG_DEBUG, "server", "Unknown channel %s\n", channame);
	}
}

/* Handle away notifications */
void server_handle_whois_301(struct server *server, struct irccmd *cmd)
{
	struct user	*u;
	struct channel	*ch;

	/* We only want these on user links */
	if (	server->type != SRV_BITLBEE &&
		server->type != SRV_USER)
	{
		dolog(LOG_DEBUG, "server", "Received 301 reply on a server link\n");
		return;
	}

	/* 0=/me, 1=nick, 2=reason */
	u = user_find_nick(cmd->p[1]);

	/* Update the away */	
	user_change_away(u, cmd->p[2]);
}

/* Handle bad nicknames */
void server_handle_badnick(struct server *server, struct irccmd *cmd)
{
	char		tmp[20];
	unsigned int	i = 0;
	struct user	*u = NULL;

	u = user_find_nick(cmd->p[1]);
	if (!u)
	{
		dolog(LOG_DEBUG, "server", "%s has a bad nick but is not known to me\n",
			cmd->p[1]);
		return;
	}

	/* Can't do anything on normal user links */
	if (u->server->type != SRV_BITLBEE) return;

	/* Try to rename the user to some standard name */
	if (!getfreenick(tmp, sizeof(tmp))) return;

	/* Remove the user from the server */
	server_leave(server, u, "Bad nickname, changing it", false);

	/* On BitlBee try to rename the user to something else */
	server_printf(u->server, "PRIVMSG #bitlbee :rename %s %s\n",
		cmd->p[1], tmp);
	return;
}

/* source=killer, p0=user, p1=reason*/
void server_handle_kill(struct server *server, struct irccmd *cmd)
{
	char		reason[1024];
	struct user	*u;
	
	u = user_find_nick(cmd->p[0]);

	if (!u)
	{
		dolog(LOG_DEBUG, "server", "%s got killed on %s:%s but is not known to me\n",
			cmd->p[0], server->hostname, server->port);
		return;
	}
	
	snprintf(reason, sizeof(reason), "Killed by %s :%s", cmd->source, cmd->p[1]);

	/* Remove the user from the server */
	server_leave(server, u, reason, true);
}

void server_handle(struct server *server)
{
	int		sret;
	unsigned int	i, j, k, loops = 0;
	char		line[BUFFERSIZE], nick[BUFFERSIZE], *c;
	struct irccmd	cmd;
	struct server	*srv = NULL;
	struct channel	*ch = NULL;
	struct user	*u = NULL;
	struct listnode	*sn, *cn, *un;

	/* Not connected? Exit, should not happen */
	if (server->socket == -1)
	{
		dolog(LOG_ERR, "server", "server_handle() with -1 socket...\n");
		exit(-1);
	}

	while ((sret = sock_getline(server->socket, server->buffer, sizeof(server->buffer), &server->bufferfill, line, sizeof(line))) > 0)
	{
		loops++;

		/* dolog(LOG_DEBUG, "server", "[%s@%s:%s] handle(%s)\n", server->name, server->hostname, server->port, line); */

		/* Update received counters */
		server->stat_recv_msg++;
		server->stat_recv_bytes += sret;

		/* Shortcut for pingponging */
		if (strncmp("PING", line, 4) == 0)
		{
			server_printf(server, "PONG%s\n", &line[4]);
			continue;
		}

		if (!server_parsestring(line, &cmd))
		{
			dolog(LOG_ERR, "server", "Parse error?\n");
			continue;
		}

		/* User related */
		if	(strcasecmp(cmd.cmd, "PRIVMSG") == 0)	server_handle_privmsg	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "QUIT") == 0)	server_handle_quit	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "MODE") == 0)	server_handle_mode	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "AWAY") == 0)	server_handle_away	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "NICK") == 0)	server_handle_nick	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "WHOIS") == 0)	server_handle_whois	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "001") == 0)	server_handle_connected	(server, &cmd);

		/* Channel related */
		else if (strcasecmp(cmd.cmd, "JOIN") == 0)	server_handle_join	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "PART") == 0)	server_handle_part	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "KICK") == 0)	server_handle_kick	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "TOPIC") == 0)	server_handle_topic	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "332") == 0)	server_handle_topic_332	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "333") == 0)	server_handle_topic_333	(server, &cmd);

		/*
		 * Silly interface commands to let people see this is a real Talamasca ;)
		 * and to make it implement most of the IRC commands
		 */
		else if (strcasecmp(cmd.cmd, "VERSION") == 0)	server_handle_version	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "INFO") == 0)	server_handle_info	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "ADMIN") == 0)	server_handle_admin	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "MOTD") == 0)	server_handle_motd	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "TIME") == 0)	server_handle_time	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "STATS") == 0)	server_handle_stats	(server, &cmd);

		/* Server<->Server commands */
		else if (strcasecmp(cmd.cmd, "SERVER") == 0)	server_handle_server	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "SJOIN") == 0)	server_handle_sjoin	(server, &cmd);

		else if (strcasecmp(cmd.cmd, "353") == 0)	server_handle_whois_353	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "311") == 0)	server_handle_whois_311	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "319") == 0)	server_handle_whois_319	(server, &cmd);
		else if (strcasecmp(cmd.cmd, "301") == 0)	server_handle_whois_301 (server, &cmd);

		else if (strcasecmp(cmd.cmd, "432") == 0)	server_handle_badnick	(server, &cmd);

		else if (strcasecmp(cmd.cmd, "KILL") == 0)	server_handle_kill	(server, &cmd);

		/* Disconnecting commands */
		else if (strcasecmp(cmd.cmd, "ERROR") == 0 ||
			 strcasecmp(cmd.cmd, "SQUIT") == 0)
		{
			/* Set an error and break out of the loop */
			sret = -2;
			break;
		}

		/* Ignores */
		else if (strcasecmp(cmd.cmd, "NOTICE") == 0 ||	/* Notice */
			 strcasecmp(cmd.cmd, "GNOTICE") == 0 ||	/* Server Notice */
			 strcasecmp(cmd.cmd, "PASS") == 0 ||	/* Password */
			 strcasecmp(cmd.cmd, "SVINFO") == 0 ||	/* Server information */
			 strcasecmp(cmd.cmd, "CAPAB") == 0 ||	/* Server capabilities */

			 strcasecmp(cmd.cmd, "002") == 0 ||	/* Server version */
			 strcasecmp(cmd.cmd, "003") == 0 ||	/* Server creation */
			 strcasecmp(cmd.cmd, "004") == 0 ||	/* Server options */
			 strcasecmp(cmd.cmd, "005") == 0 ||	/* Server haves */

			 strcasecmp(cmd.cmd, "221") == 0 ||	/* User mode (set by server) */

			 strcasecmp(cmd.cmd, "251") == 0 ||	/* stat: user count */
			 strcasecmp(cmd.cmd, "252") == 0 ||	/* stat: # IRC Operators */
			 strcasecmp(cmd.cmd, "253") == 0 ||	/* stat: # Unknown Connection */
			 strcasecmp(cmd.cmd, "254") == 0 ||	/* stat: # channels */
			 strcasecmp(cmd.cmd, "255") == 0 ||	/* stat: # clients & servers */
			 strcasecmp(cmd.cmd, "265") == 0 ||	/* stat: # local users */
			 strcasecmp(cmd.cmd, "266") == 0 ||	/* stat: # global users */

			 strcasecmp(cmd.cmd, "312") == 0 ||	/* whois: server */
			 strcasecmp(cmd.cmd, "317") == 0 ||	/* whois: idle/signon */
			 strcasecmp(cmd.cmd, "318") == 0 ||	/* whois: end */
			 
			 strcasecmp(cmd.cmd, "366") == 0 ||	/* names: end */

			 strcasecmp(cmd.cmd, "372") == 0 ||	/* motd: line */
			 strcasecmp(cmd.cmd, "375") == 0 ||	/* motd: start */
			 strcasecmp(cmd.cmd, "376") == 0 ||	/* motd: end */
			 
			 strcasecmp(cmd.cmd, "401") == 0 ||	/* Unknown nick/channel */
			 strcasecmp(cmd.cmd, "442") == 0)	/* User is not on that channel */
		{
			/* Ignore */
		}
		else
		{
			dolog(LOG_DEBUG, "server", "[%s@%s:%s] Ignoring unknown cmd '%s'\n", server->name, server->hostname, server->port, cmd.cmd);
		}
	}

	if (sret == 0)
	{
		/*dolog(LOG_DEBUG, "server", "Didn't receive a thing on %s:%s after %u loops\n", server->hostname, server->port, loops);*/
		if (loops == 0) server_disconnect(server);
		return;
	}
	else if (sret < 0)
	{
		dolog(LOG_DEBUG, "server", "[%s@%s:%s] Got an error (%i), disconnecting: %s (%d)\n", server->name, server->hostname, server->port, sret, strerror(errno), errno);
		server_disconnect(server);
	}
}

