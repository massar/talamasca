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
 User handling
******************************************************/

#include "talamasca.h"

struct user *user_add(char *nick, struct server *server, bool config)
{
	struct user *user = malloc(sizeof(*user));
	
	if (!server)
	{
		dolog(LOG_ERR, "user", "Even %s can't live without a server!\n", nick);
		return NULL;
	}
	if (!user)
	{
		dolog(LOG_ERR, "user", "Not enough memory left to create a new user!?\n");
		exit(-1);
	}

	if (server) dolog(LOG_DEBUG, "user", "user_add(%s,%s:%s)\n", nick, server->hostname, server->port);
	else dolog(LOG_DEBUG, "user", "user_add(%s)\n", nick);

	/* Initialize */
	memset(user, 0, sizeof(*user));
	user->nick		= strdup(nick);
	user->server		= server;
	user->channels		= list_new();
	user->channels->del 	= NULL;
	user->config		= config;

	/* Login time is last message time */
	user->lastmessage	= time(NULL);

	/* Add the user */
	listnode_add(g_conf->users, user);
	return user;
}

void user_resetidle(struct user *user)
{
	user->lastmessage = time(NULL);
}

void user_change_ident(struct user *user, char *ident)
{
	if (!user)
	{
		dolog(LOG_DEBUG, "user", "user_change_ident() - Something passed me a NULL user!\n");
		return;
	}
	if (user->ident)	free(user->ident);
	if (ident)		user->ident = strdup(ident);
	else			user->ident = NULL;
}

void user_change_host(struct user *user, char *host)
{
	if (!user)
	{
		dolog(LOG_DEBUG, "user", "user_change_host() - Something passed me a NULL user!\n");
		return;
	}
	if (user->host)		free(user->host);
	if (host)		user->host = strdup(host);
	else			user->host = NULL;
}

void user_change_realname(struct user *user, char *realname)
{
	if (!user)
	{
		dolog(LOG_DEBUG, "user", "user_change_realname() - Something passed me a NULL user!\n");
		return;
	}
	if (user->realname)	free(user->realname);
	if (realname)		user->realname = strdup(realname);
	else			user->realname = NULL;
}

void user_introduce(struct user *user)
{
	struct server	*srv;
	struct listnode	*ln;
	
	if (!user->ident || !user->host || !user->realname)
	{
		dolog(LOG_ERR, "user", "Not introducing user %s!%s@%s (%s), some are empty",
			user->nick, user->ident, user->host, user->realname);
		return;
	}

	/* Introduce the user to the servers we have */
	LIST_LOOP(g_conf->servers, srv, ln)
	{
		server_introduce(srv, user);
	}
}

void user_leave(struct user *user, char *reason)
{
	struct server	*srv;
	struct channel	*ch;
	struct listnode	*ln;
	unsigned int	i;

	dolog(LOG_DEBUG, "user", "Taking user %s!%s@%s from the channels\n", user->nick, user->ident, user->host);

	/* Remove the user from all channels she is on */
	while (user->channels->count > 0)
	{
		LIST_LOOP(user->channels, ch, ln)
		{
			dolog(LOG_DEBUG, "user", "Removing %s!%s@%s from %s (%u channels left)\n",
				user->nick, user->ident, user->host, ch->name, user->channels->count);
			/* Remove the user from the channel */
			channel_deluser(ch, user, "Quiting...", true);
			/* Start checking at the beginning as the list changed */
			break;
		}
		if (!ln) break;
	}

	dolog(LOG_DEBUG, "user", "Taking user %s!%s@%s from global user list\n", user->nick, user->ident, user->host);

	/* Quit the user from the servers we have */
	LIST_LOOP(g_conf->servers, srv, ln)
	{
		server_leave(srv, user, reason, false);
	}
}

void user_destroy(struct user *user, char *reason)
{
	struct channel	*channel;
	struct listnode	*cn, *cn2;

	if (!user) return;

	dolog(LOG_DEBUG, "user", "Destroying user %s!%s@%s\n", user->nick, user->ident, user->host);

	/* Let the user leave all the servers */
	user_leave(user, reason);

	/* Remove the user from the global user list */
	listnode_delete(g_conf->users, user);

	/* The last log message about this user */
	dolog(LOG_DEBUG, "user", "User %s!%s@%s is goners\n", user->nick, user->ident, user->host);

	/* Free the node */
	if (user->nick)		free(user->nick);
	if (user->ident)	free(user->ident);
	if (user->host)		free(user->host);
	if (user->realname)	free(user->realname);
	if (user->away)		free(user->away);

	/* Free the memory */
	free(user);
}

struct user *user_find_nick(char *nick)
{
	struct user	*u;
	struct listnode	*un;

	if (!nick)
	{
		dolog(LOG_ERR, "user", "user_find_nick() - Something passed me a NULL nick!\n");
		return NULL;
	}
	LIST_LOOP(g_conf->users, u, un)
	{
		if (strcasecmp(u->nick, nick) == 0) return u;
	}
	return NULL;
}

void user_change_away(struct user *user, char *reason)
{
	if (!user)
	{
		dolog(LOG_ERR, "user", "user_change_away() - Something passed me a NULL user!\n");
		return;
	}

	if (user->away)
	{
		/* Don't touch when it is still the same */
		if (reason && strcasecmp(user->away, reason) == 0) return;
		free(user->away);
	}
	if (reason)	user->away = strdup(reason);
	else		user->away = NULL;

	if (user->away) dolog(LOG_DEBUG, "user", "User %s's away: %s\n", user->nick, user->away);
	else dolog(LOG_DEBUG, "user", "User %s is back\n", user->nick);
}

void user_change_nick(struct user *user, char *newnick, bool local)
{
	struct server	*srv = NULL;
	struct listnode	*sn = NULL;
	char		*oldnick = NULL;

	if (!user)
	{
		dolog(LOG_ERR, "user", "user_changenick(%s) - Something passed me a NULL user!\n", newnick);
		return;
	}

	dolog(LOG_DEBUG, "user", "Changing %s!%s@%s's nick to %s\n",
		user->nick, user->ident, user->host, newnick);

	/* Keep the oldnick for a moment */
	oldnick = user->nick;

	/* Change change it */
	user->nick = strdup(newnick);

	LIST_LOOP(g_conf->servers, srv, sn)
	{
		/* Don't change it on the machine itself when it was a remote change */
		if (!local && user->server == srv) continue;

		server_user_change_nick(srv, user, oldnick);
	}

	/* Throw away the old nickname */
	free(oldnick);
}
