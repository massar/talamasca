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
 Channel handling
******************************************************/

#include "talamasca.h"

struct channel *channel_find_tag(char *tag)
{
	struct server	*srv;
	struct channel	*ch;
	struct listnode	*ln, *ln2;

	LIST_LOOP(g_conf->servers, srv, ln)
	{
		LIST_LOOP(srv->channels, ch, ln2)
		{
			if (strcasecmp(ch->tag, tag) == 0) return ch;
		}
	}
	return NULL;
}

void channeluser_destroy(struct channeluser *cu)
{
	if (!cu) return;
	if (cu->user) listnode_delete(cu->user->channels, cu->channel);
	free(cu);
}

struct channel *channel_add(struct server *server, char *name, char *tag)
{
	struct channel *channel = malloc(sizeof(*channel));

	if (!channel)
	{
		dolog(LOG_ERR, "channel", "Not enough memory left to create a new channel!?\n");
		exit(-1);
	}

	/* Initialize */
	memset(channel, 0, sizeof(*channel));

	if (tag) channel->tag	= strdup(tag);
	channel->name		= strdup(name);
	channel->server		= server;

	channel->users		= list_new();
	channel->users->del 	= (void(*)(void *))channeluser_destroy;

	/* Add it to the list */
	listnode_add(server->channels, channel);
	
	dolog(LOG_DEBUG, "channel", "channel_add(%s on %s:%s)\n", name, server->hostname, server->port);

	return channel;
}

struct channeluser *channel_find_user(struct channel *channel, struct user *user)
{
	struct channeluser	*cu;
	struct listnode		*ln;
	
	if (!channel)
	{
		dolog(LOG_ERR, "channel", "channel_find_user() - Something passed me a NULL channel!\n");
		return NULL;
	}
	if (!user)
	{
		dolog(LOG_ERR, "channel", "channel_find_user() - Something passed me a NULL user!\n");
		return NULL;
	}

	LIST_LOOP(channel->users, cu, ln)
	{
		if (cu->user == user) return cu;
	}

	return NULL;
}

/* Send a message to a channel */
void channel_message(struct channel *channel, struct user *user, char *message, ...)
{
	char			buf[2048];
	struct channeluser	*cu;
	struct listnode		*ln;
	va_list ap;
	
	if (!channel || !user || !message) return;

	va_start(ap, message);
	/* Format the string */
	vsnprintf(buf, sizeof(buf), message, ap);
	va_end(ap);

	if (	channel->server->type == SRV_RFC1459 ||
		channel->server->type == SRV_TS)
	{
		/* Ignore it when there is no source user */
		if (!user) return;

		/* Show it using a privmsg */
		server_printf(channel->server, ":%s PRIVMSG %s :%s\n",
			user->nick, channel->name, buf);
		return;
	}
	
	if (channel->server->type == SRV_P10)
	{
		/* Ignore it when there is no source user */
		if (!user) return;

		dolog(LOG_ERR, "channel", "channel_message() - P10 not supported -> exit\n");
		exit(-42);
	}

	/* Bitlbee */
	if (channel->server->type == SRV_BITLBEE)
	{
		bool prefix = strncmp(buf, "### ", 4) == 0 || !user ? false : true;

		/* Notify all the bitlbee users on the channel */
		LIST_LOOP(channel->users, cu, ln)
		{
			/*
			 * - Don't send it to itself
			 * - or to users on another server
			 * - or when it is the server user
			 */
			if (	user == cu->user ||
				channel->server != cu->user->server ||
				cu->user == channel->server->user) continue;

			/* Show it using a privmsg */
			server_printf(channel->server,
				"PRIVMSG %s :%s%s%s\n",
				cu->user->nick,
				prefix ? user->nick : "", prefix ? ": " : "",
				buf);
		}
		return;
	}

	/* Normal user connection */
	server_printf(channel->server,
		"PRIVMSG %s :%s%s%s\n",
		channel->name,
		user ? user->nick : "",
		user ? ": " : "",
		buf);
	return;
}

void channel_introduce(struct channel *channel, struct user *user)
{
	struct channeluser *cu;

	if (!channel || !user) return;

	dolog(LOG_DEBUG, "channel", "channel_introduce %s!%s@%s to %s on %s:%s\n",
		user->nick, user->ident, user->host,
		channel->name,
		channel->server->hostname, channel->server->port);

	/* Don't introduce server users */	
	if (user == user->server->user)
	{
		dolog(LOG_DEBUG, "channel", "Ignoring introduction of server user %s\n", user->nick);
		return;
	}

	/* Try to find the user in the channel */
	cu = channel_find_user(channel, user);
	
	if (cu && cu->introduced)
	{
		dolog(LOG_DEBUG, "channel", "User %s!%s@%s already introduced to channel %s on %s:%s\n",
			user->nick, user->ident, user->host,
			channel->name, channel->server->hostname, channel->server->port);
		return;
	}

	/* Not added yet? */
	if (!cu)
	{
		cu = malloc(sizeof(*cu));
		if (!cu)
		{
			dolog(LOG_ERR, "channel", "channel_introduce() Couldn't allocate memory for serveruser\n");
			exit(-42);
		}
		memset(cu, 0, sizeof(*cu));
		
		cu->channel = channel;
		cu->user = user;

		/* Add the user to the channel member list */
		listnode_add(channel->users, cu);
		
		/* Also add a backreference */
		listnode_add(user->channels, channel);
	}

	if (channel->server->state != SS_CONNECTED)
	{
		dolog(LOG_DEBUG, "channel", "Server %s:%s is not connected, thus cannot introduce user %s to channel %s\n",
			channel->server->hostname, channel->server->port, user->nick, channel->name);
		return;
	}

	/* We'll introduce this person to the channel in a few... */
	cu->introduced = true;

	/*
	 * Don't introduce users on their own server
	 * unless it is a userlink/BitlBee server
	 */
	if (	channel->server == user->server &&
		channel->server->type != SRV_USER &&
		channel->server->type != SRV_BITLBEE)
	{
		dolog(LOG_DEBUG, "channel", "Not introducing user onto own server\n");
		return;
	}

	/*
	 * Introduce users only to Server<->Server links
	 */
	if (	channel->server->type == SRV_RFC1459 ||
		channel->server->type == SRV_TS)
	{
		/* Introduce this user to the channel */
		server_printf(channel->server,
			":%s SJOIN %u %u %s + :%s\n",
			channel->server->name, time(NULL), time(NULL), channel->name, user->nick);
	}
	else if (channel->server->type == SRV_P10)
	{
		exit(-42);
	}
	else
	{
		channel_message(channel, user, "### %s (%s@%s) joined the channel\n",
			user->nick, user->ident, user->host);
	}

	return;
}

void channel_leave(struct channel *channel, struct user *user, char *reason, bool notify)
{
	struct channeluser *cu;

	if (!channel || !user) return;

	dolog(LOG_DEBUG, "channel", "channel_leave(%s: %s!%s@%s)\n", channel->name, user->nick, user->ident, user->host);

	/* Try to find the user on the channel */
	cu = channel_find_user(channel, user);
	
	if (!cu)
	{
		dolog(LOG_DEBUG, "channel", "User %s!%s@%s was not on channel %s on %s:%s\n",
			user->nick, user->ident, user->host,
			channel->name, channel->server->hostname, channel->server->port);
		return;
	}

	/* Do we need to part this user from the channel? */
	if (cu->introduced && notify)
	{
		if (	channel->server->type == SRV_RFC1459 ||
			channel->server->type == SRV_TS)
		{
			/* Quit this user from the server */
			server_printf(channel->server,
				":%s PART %s :%s\n",
				user->nick, channel->name, reason ? reason : "Leaving");
		}
		else if (channel->server->type == SRV_P10)
		{
			/* Not supported */
			exit(-42);
		}
		else
		{
			channel_message(channel, user, "### %s (%s@%s) parted the channel (%s)\n",
				user->nick, user->ident, user->host, reason ? reason : "Leaving");
		}
	}

	/* Remove the user from the user list */
	listnode_delete(channel->users, cu);

	/* Remove the channel from the user's list */
	listnode_delete(user->channels, channel);

	dolog(LOG_DEBUG, "channel", "channel_leave(%s: %s!%s@%s) - LEFT\n", channel->name, user->nick, user->ident, user->host);
}

void channel_link(struct channel *channel, struct channel *link)
{
	struct channeluser	*cu;
	struct listnode		*ln;

	/* Cross link the channels */
	channel->link = link;
	link->link = channel;

	/* Introduce users to eachother */
	LIST_LOOP(channel->users, cu, ln)
	{
		channel_introduce(link, cu->user);
	}
	LIST_LOOP(link->users, cu, ln)
	{
		channel_introduce(channel, cu->user);
	}
}

void channel_adduser(struct channel *channel, struct user *user)
{
	if (!channel || !user) return;

	dolog(LOG_DEBUG, "channel", "channel_adduser(%s, %s)\n", channel->name, user->nick);
	/* Introduce the user on the channel */
	channel_introduce(channel, user);
	/* And also introduce the user on the linked channel */
	if (channel->link) channel_introduce(channel->link, user);
}

void channel_deluser(struct channel *channel, struct user *user, char *reason, bool notify)
{
	if (!channel || !user) return;

	dolog(LOG_DEBUG, "channel", "channel_deluser(%s, %s)\n", channel->name, user->nick);
	/* Let the user leave the channel */
	channel_leave(channel, user, reason, notify);
	/* And also leave the linked channel */
	if (channel->link)
	{
		dolog(LOG_DEBUG, "channel", "channel_deluser(%s, %s) link of %s\n", channel->link->name, user->nick, channel->name);
		channel_leave(channel->link, user, reason, true);
	}
}

void channel_destroy(struct channel *channel)
{
	if (!channel) return;

	dolog(LOG_DEBUG, "channel", "channel_destroy(%s)\n", channel->name);

	/* Remove the link between the channels */
	if (channel->link)
	{
		channel->link->link = NULL;
		channel->link = NULL;
	}

	/* Purge the users from this channel */
	list_delete(channel->users);

	if (channel->server)	listnode_delete(channel->server->channels, channel);
	if (channel->name)	free(channel->name);
	if (channel->tag)	free(channel->tag);
	if (channel->topic)	free(channel->topic);
	if (channel->topic_who)	free(channel->topic_who);
	if (channel->key)	free(channel->key);

	free(channel);
}

void channel_change_topic(struct channel *channel, char *topic)
{
	if (!channel)
	{
		dolog(LOG_DEBUG, "channel", "channel_change_topic() - Something passed me a NULL channel!\n");
		return;
	}
	if (channel->topic)	free(channel->topic);
	if (topic)		channel->topic = strdup(topic);
	else			channel->topic = NULL;
}

void channel_change_topic_who(struct channel *channel, char *who)
{
	if (!channel)
	{
		dolog(LOG_DEBUG, "channel", "channel_change_topic_who() - Something passed me a NULL channel!\n");
		return;
	}
	if (channel->topic_who)	free(channel->topic_who);
	if (who)		channel->topic_who = strdup(who);
	else			channel->topic_who = NULL;
}

void channel_change_topic_when(struct channel *channel, time_t when)
{
	if (!channel)
	{
		dolog(LOG_DEBUG, "channel", "channel_change_topic_who() - Something passed me a NULL channel!\n");
		return;
	}
	if (when == 0) when = time(NULL);
	channel->topic_when = when;
}

void channel_change_key(struct channel *channel, char *key)
{
	if (!channel)
	{
		dolog(LOG_DEBUG, "channel", "channel_change_key() - Something passed me a NULL channel!\n");
		return;
	}
	if (channel->key)	free(channel->key);
	if (key)		channel->key = strdup(key);
	else			channel->key = NULL;
}
