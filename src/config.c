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
 Talamasca Configuration & Monitoring
*******************************************************

Reply codes:
 2xx = Ok
 3xx = Warning
 4xx = Error
 5xx = Fatal error

 x00 = Single Line response
 x01 = Multi Line response upto the x02 eg:
 x02 
    C: list
    S: 201 Listing following...
    S: aap
    S: noot
    S: mies
    S: 202 Listing done
 x03 = Authentication required

Login procedure:
 - client connects
 S: 200 The Talamasca (C) Copyright Jeroen Massar 2004 All Rights Reserved on talamasca.unfix.org

 - Request the server for a challenge
 C: login jeroen
 S: 200 e73bd13fd082d0e4a1bdbd9c28ad28ac

 - response = md5("password" + challenge)
 C: authenticate 5c8fe9a593da4e2f879020396d0fe90f
 S: 200 Welcome to The Talamasca
***********************************************************/

#include "talamasca.h"

enum cfg_level
{
	LEVEL_NONE,			/* Can always be used */
	LEVEL_LOGIN,			/* Login most be given, and thus a challenge is available */
	LEVEL_AUTH,			/* Normal status and information commands */
	LEVEL_CONFIG,			/* Configuration commands */

	LEVEL_MAXIMUM			/* All permissions */
};

struct cfg_state
{
	enum cfg_level	level;				/* If we are authenticated or not */
	bool		quit;				/* End this commander? */
	SOCKET 		sock;				/* The socket we are using for communications */
	int		protocol;			/* Protocol for this connection (AF_*) */
	char		challenge[64];			/* The challenge we present to the client */
	char		clienthost[NI_MAXHOST];		/* Host of the client */
	char		clientservice[NI_MAXSERV];	/* Service of the client */
	char		user[200];			/* Username */

	char		padding[3];			/* Padding */
};

/********************************************************************
  Commands
********************************************************************/
bool cfg_misc_help(struct cfg_state *cmd, char *args);
/* Defined after cmd_cmds */

bool cfg_misc_reply(struct cfg_state *cmd, char *args)
{
	sock_printf(cmd->sock, "200 %s\n", args);
	return true;
}

bool cfg_info_status(struct cfg_state *cmd, char *args)
{
	int	fields = countfields(args);
	char	buf[42];

	sock_printf(cmd->sock, "201 Status\n", buf);
	sock_printf(cmd->sock, "I am running ;)\n", buf);
	sock_printf(cmd->sock, "202 Status complete\n", buf);
	return true;
}

bool cfg_conf_set(struct cfg_state *cmd, char *args)
{
	int	fields = countfields(args);
	char	var[50], val[1024], val2[1024], val3[1024];

	/* Require a value */
	if (	fields < 2 ||
		!copyfield(args, 1, var, sizeof(var)) ||
		!copyfield(args, 2, val, sizeof(val)))
	{
		sock_printf(cmd->sock, "400 The command is: set <variable> <value> [<value> ...]\n");
		return false;
	}

	if (fields > 2) copyfield(args, 3, val2, sizeof(val2));
	if (fields > 3) copyfields(args, 4, 0, val3, sizeof(val3));

	if (strcasecmp(var, "service_name") == 0 && fields == 2)
	{
		if (g_conf->service_name) free(g_conf->service_name);
		g_conf->service_name = strdup(val);
		return true;
	}
	if (strcasecmp(var, "service_description") == 0 && fields == 2)
	{
		if (g_conf->service_description) free(g_conf->service_description);
		g_conf->service_description = strdup(val);
		return true;
	}
	if (strcasecmp(var, "admin_location1") == 0 && fields == 2)
	{
		if (g_conf->admin_location1) free(g_conf->admin_location1);
		g_conf->admin_location1 = strdup(val);
		return true;
	}
	if (strcasecmp(var, "admin_location2") == 0 && fields == 2)
	{
		if (g_conf->admin_location2) free(g_conf->admin_location2);
		g_conf->admin_location2 = strdup(val);
		return true;
	}
	if (strcasecmp(var, "admin_email") == 0 && fields == 2)
	{
		if (g_conf->admin_email) free(g_conf->admin_email);
		g_conf->admin_email = strdup(val);
		return true;
	}
	if (strcasecmp(var, "motd_file") == 0 && fields == 2)
	{
		if (g_conf->motd_file) free(g_conf->motd_file);
		g_conf->motd_file = strdup(val);
		return true;
	}
	if (strcasecmp(var, "config_password") == 0 && fields == 2)
	{
		if (g_conf->config_password) free(g_conf->config_password);
		g_conf->config_password = (unsigned char *)strdup(val);
		return true;
	}
	if (strcasecmp(var, "bitlbee_auto_add") == 0 && fields == 2)
	{
		if (	strcasecmp(val, "true") == 0 ||
			strcasecmp(val, "on") == 0)
		{
			g_conf->bitlbee_auto_add = true;
		}
		else g_conf->bitlbee_auto_add = false;
		return true;
	}

	if (strcasecmp(var, "verbose") == 0 && fields == 2)
	{
		if (	strcasecmp(val, "on") == 0 ||
			strcasecmp(val, "true") == 0 )
		{
			g_conf->verbose = true;

			/* Report back */
			sock_printf(cmd->sock, "200 Verbosity Activated\n");
			return true;
		}
		else if (strcasecmp(val, "off") == 0 ||
			strcasecmp(val, "false") == 0 )
		{
			g_conf->verbose = true;

			/* Report back */
			sock_printf(cmd->sock, "200 Verbosity Disabled\n");
			return true;
		}
		sock_printf(cmd->sock, "400 verbose only accepts 'on' and 'off' and not '%s'\n", var);
		return false;
	}

	sock_printf(cmd->sock, "400 Unknown settable value '%s' with %u fields\n", var, fields);
	return false;
}

/* server add <servertag> <RFC1459|Timestamp|P10|User|BitlBee> <hostname> <service|portnumber> <nickname|none> <localname> <password|none> <identity> */
bool cfg_conf_server_add(struct cfg_state *cmd, char *args)
{
	int		fields = countfields(args);
	char		tag[25], type[10], hostname[24], service[24], nick[24], local[24], pass[24], identity[24];
	enum srv_types	typ = SRV_BITLBEE;

	/* Add requires 8 variables */
	if (	fields != 8 ||
		!copyfield(args, 1, tag,	sizeof(tag)) ||
		!copyfield(args, 2, type,	sizeof(type)) ||
		!copyfield(args, 3, hostname,	sizeof(hostname)) ||
		!copyfield(args, 4, service,	sizeof(service)) ||
		!copyfield(args, 5, nick,	sizeof(nick)) ||
		!copyfield(args, 6, local,	sizeof(local)) ||
		!copyfield(args, 7, pass,	sizeof(pass)) ||
		!copyfield(args, 8, identity,	sizeof(identity)))
	{
		sock_printf(cmd->sock, "400 The command is: server add <servertag> <RFC1459|Timestamp|P10|User|BitlBee> <hostname> <service|portnumber> <nickname|none> <localname> <password> <identity>\n");
		return false;
	}

	if (	 strcasecmp(type, "rfc1459"	) == 0) typ = SRV_RFC1459;
	else if (strcasecmp(type, "timestamp"	) == 0) typ = SRV_TS;
	else if (strcasecmp(type, "bitlbee"	) == 0) typ = SRV_BITLBEE;
	else if (strcasecmp(type, "user"	) == 0) typ = SRV_USER;
	else
	{
		sock_printf(cmd->sock, "400 '%s' is an unsupported server type\n", type);
		return false;
	}
	
	if (server_find_tag(tag))
	{
		sock_printf(cmd->sock, "400 Server '%s' already exists\n", tag);
		return false;
	}

	if (server_add(tag, typ, hostname, service,
		strcasecmp(nick, "none") == 0 ? NULL : nick,
		local,
		strcasecmp(pass, "none") == 0 ? NULL : pass,
		identity, g_conf->service_description))
	{
		sock_printf(cmd->sock, "200 Added server %s\n", tag);
		return true;
	}
	sock_printf(cmd->sock, "400 Server addition failed\n");
	return false;
}

/* server set <servertag> <variable> <value> */
bool cfg_conf_server_set(struct cfg_state *cmd, char *args)
{
	int		fields = countfields(args);
	char		tag[25], var[25], val[1000];
	struct server	*srv;
	struct channel	*ch;

	/* Add requires 8 variables */
	if (	fields != 3 ||
		!copyfield(args, 1, tag, sizeof(tag)) ||
		!copyfield(args, 2, var, sizeof(var)) ||
		!copyfield(args, 3, val, sizeof(val)))
	{
		sock_printf(cmd->sock, "400 The command is: server set <servertag> <variable> <value>\n");
		return false;
	}

	srv = server_find_tag(tag);

	if (!srv)
	{
		sock_printf(cmd->sock, "400 Server '%s' does not exist\n");
		return false;
	}

	if (strcasecmp(var, "bitlbee_identifypass") == 0)
	{
		if (srv->bitlbee_identifypass) free(srv->bitlbee_identifypass);
		srv->bitlbee_identifypass = strdup(val);
		
		sock_printf(cmd->sock, "200 Configured the BitlBee password\n");
		return true;
	}

	if (strcasecmp(var, "defaultchannel") == 0)
	{
		if (	srv->type != SRV_USER &&
			srv->type != SRV_BITLBEE)
		{
			sock_printf(cmd->sock, "400 Defaultchannels are only required for user and BitlBee links\n", val);
			return false;
		}

		ch = channel_find_tag(val);
		if (!ch)
		{
			sock_printf(cmd->sock, "400 Channel '%s' does not exist\n", val);
			return false;
		}
		srv->defaultchannel = ch;

		/* Make sure that our user is also there */
		channel_adduser(ch, srv->user);

		sock_printf(cmd->sock, "200 Configured the default channel\n");
		return true;
	}

	sock_printf(cmd->sock, "400 Unknown settable value '%s'\n", var);
	return false;
}

/* server connect <servertag> */
bool cfg_conf_server_connect(struct cfg_state *cmd, char *args)
{
	int		fields = countfields(args);
	char		tag[25], var[25], val[1000];
	struct server	*srv;
	struct channel	*ch;

	/* Add requires 1 variables */
	if (	fields != 1 ||
		!copyfield(args, 1, tag, sizeof(tag)))
	{
		sock_printf(cmd->sock, "400 The command is: server connect <servertag>\n");
		return false;
	}

	srv = server_find_tag(tag);

	if (!srv)
	{
		sock_printf(cmd->sock, "400 Server '%s' does not exist\n");
		return false;
	}
	
	server_connect(srv);

	sock_printf(cmd->sock, "200 Connecting to server\n");
	return true;
}

bool cfg_conf_server(struct cfg_state *cmd, char *args)
{
	if (strncasecmp(args, "add ", 4) == 0)
	{
		return cfg_conf_server_add(cmd, &args[4]);
	}
	if (strncasecmp(args, "set ", 4) == 0)
	{
		return cfg_conf_server_set(cmd, &args[4]);
	}
	if (strncasecmp(args, "connect ", 8) == 0)
	{
		return cfg_conf_server_connect(cmd, &args[8]);
	}
	sock_printf(cmd->sock, "400 Unknown command\n");
	return false;
}

/* channel add <servertag> <channeltag> <name> */
bool cfg_conf_channel_add(struct cfg_state *cmd, char *args)
{
	int		fields = countfields(args);
	char		stag[24], ctag[24], name[24];
	struct server	*srv;

	/* Add requires 3 variables */
	if (	fields != 3 ||
		!copyfield(args, 1, stag,	sizeof(stag)) ||
		!copyfield(args, 2, ctag,	sizeof(ctag)) ||
		!copyfield(args, 3, name,	sizeof(name)))
	{
		sock_printf(cmd->sock, "400 The command is: channel add <servertag> <channeltag> <name>\n");
		return false;
	}
	
	srv = server_find_tag(stag);
	if (!srv)
	{
		sock_printf(cmd->sock, "400 Server '%s' does not exist\n", stag);
		return false;
	}

	if (channel_find_tag(ctag))
	{
		sock_printf(cmd->sock, "400 Channel '%s' does already exist\n", ctag);
		return false;
	}

	if (channel_add(srv, name, ctag))
	{
		sock_printf(cmd->sock, "200 Added channel %s\n", ctag);
		return true;
	}
	sock_printf(cmd->sock, "400 Channel addition failed\n");
	return false;
}

/* channel link <channeltag> <channeltag> */
bool cfg_conf_channel_link(struct cfg_state *cmd, char *args)
{
	int		fields = countfields(args);
	char		tag_a[25], tag_b[25];
	struct channel	*ch_a, *ch_b;

	/* Link requires 2 variables */
	if (	fields != 2 ||
		!copyfield(args, 1, tag_a, sizeof(tag_a)) ||
		!copyfield(args, 2, tag_b, sizeof(tag_b)))
	{
		sock_printf(cmd->sock, "400 The command is: channel link <channeltag> <channeltag>\n");
		return false;
	}

	ch_a = channel_find_tag(tag_a);
	if (!ch_a)
	{
		sock_printf(cmd->sock, "400 Channel '%s' does not exist\n", tag_a);
		return false;
	}
	ch_b = channel_find_tag(tag_b);
	if (!ch_b)
	{
		sock_printf(cmd->sock, "400 Channel '%s' does not exist\n", tag_b);
		return false;
	}

	channel_link(ch_a, ch_b);

	sock_printf(cmd->sock, "200 Channels are linked\n");
	return true;
}

bool cfg_conf_channel(struct cfg_state *cmd, char *args)
{
	if (strncasecmp(args, "add ", 4) == 0)
	{
		return cfg_conf_channel_add(cmd, &args[4]);
	}
	if (strncasecmp(args, "link ", 5) == 0)
	{
		return cfg_conf_channel_link(cmd, &args[5]);
	}
	sock_printf(cmd->sock, "400 Unknown command\n");
	return false;
}

bool cfg_misc_quit(struct cfg_state *cmd, char *args)
{
	char *byers[] = {
		"Zij die gaan, groeten u",
		"See you later alligator",
		"A bitter thought, but I have to go",
		"This is not our farewell",
		"Something I can never have",
		"Ajuuu paraplu",
		"Thank you for the information",
		"It was lovely talking to you again",
		"And All That Could Have Been...",
		"Tschussss...",
		"Aufwiedersehen",
		"Till the next time",
		"I'll be back. Ha, you didn't know I was going to say that!",
		"We will be the only two people left in the world, Yes--Adam and Evil!",
		"Tschau!",
		"Ciao",
        };
	sock_printf(cmd->sock, "200 %s\n", byers[random()%(sizeof(byers)/sizeof(char *))]);

	/* Set the quit flag */
	cmd->quit = true;

	/* This command succeeded */
	return true;
}

bool cfg_auth_login(struct cfg_state *cmd, char *args)
{
	unsigned char		secret[20] = "TheTalamasca", c, challenge[16];
	unsigned int		i,r;
	struct MD5Context	md5;
	int			fields = countfields(args);

	/* Require a username */
	if (fields != 1 ||
		!copyfield(args, 1, cmd->user, sizeof(cmd->user)))
	{
		sock_printf(cmd->sock, "400 The command is: login <username>\n");
		return false;
	}

	/*
	 * Check that the username is in lower case
	 * and contains only ascii
	 */
	for (i=0; i < strlen(cmd->user); i++)
	{
		if (	cmd->user[i] < 'a' ||
			cmd->user[i] > 'z')
		{
			sock_printf(cmd->sock, "400 Username contains unacceptable characters\n");
			return false;
		}
	}

	/* Clear out the secret */
	memset(secret, 0, sizeof(secret));

	/* Randomize the string */
	for (i=0; i < (sizeof(secret)-1); i++)
	{
		/* Pick random number between 1 and 62 */
		r = (random() % 63) + 1;

		/* [ 1,10] => [0,9] */
		if (r < 11)		c = (r+48-1);
		/* [11,36] => [A,Z] */
		else if (r < 37)	c = (r+65-10);
		/* [37,62] => [a,z] */
		else			c = (r+97-36);

		/* Randomize */
		secret[i] = c;
	}

	/* Generate a MD5 */
	MD5Init(&md5);
	MD5Update(&md5, (const unsigned char *)&secret, (unsigned int)strlen((const char *)&secret));
	MD5Final(challenge, &md5);

	memset(&cmd->challenge, 0, sizeof(cmd->challenge));

	/* Generate the Digest */
        for (i = 0; i < sizeof(challenge); i++)
        {
                snprintf(&cmd->challenge[i*2], 3, "%02x", challenge[i]);
        }

	/* Return the challenge to the client */
	sock_printf(cmd->sock, "200 %s\n", cmd->challenge);

	/* Upgrade the level */
	cmd->level = LEVEL_LOGIN;

	return true;
}

bool cfg_auth_authenticate(struct cfg_state *cmd, char *args)
{
	struct MD5Context	md5;
	int			fields = countfields(args);
	char			buf[256], res[256];
	unsigned char		challenge[20];
	int			i;

	if (	fields != 1 ||
		!copyfield(args, 1, buf, sizeof(buf)))
	{
		sock_printf(cmd->sock, "400 Command is: authenticate <response>\n");
		return false;
	}

	/* Generate a MD5 from the password and the challenge */
	MD5Init(&md5);
	MD5Update(&md5, g_conf->config_password, (unsigned int)strlen((const char *)g_conf->config_password));
	MD5Update(&md5, (const unsigned char *)&cmd->challenge, (unsigned int)strlen((const char *)&cmd->challenge));
	MD5Final(challenge, &md5);

	memset(res, 0, sizeof(res));

	/* Generate the Digest */
        for (i = 0; i < 16; i++)
        {
                snprintf(&res[i*2], 3, "%02x", challenge[i]);
        }

	if (strcmp(buf, res) != 0)
	{
		sock_printf(cmd->sock, "400 Incorrect authentication token (user: '%s', us: '%s')\n", buf, res);
		return false;
	}

	sock_printf(cmd->sock, "200 Welcome to The Talamasca (C) Copyright Jeroen Massar 2004 All Rights Reserved on %s\n", g_conf->service_name);

	/*
	 * The user logged in so set the level and title accordingly
	 * At the moment we don't have any user classes, thus
	 * we simply set everybody to allow Configuration commands
	 */
	cmd->level = LEVEL_CONFIG;
	return true;
}

/* Commands as seen above */
struct {
	char		*cmd;
	enum cfg_level	level;
	bool		(*func)(struct cfg_state *cmd, char *args);
	char		*desc;
} cfg_cmds[] = 
{
	/* Authentication */
	{"login",		LEVEL_NONE,	cfg_auth_login,		"login <username>"},
	{"authenticate",	LEVEL_LOGIN,	cfg_auth_authenticate,	"authenticate <response>"},

	/* Information */
	{"status",		LEVEL_AUTH,	cfg_info_status,	"status"},
	{"set",			LEVEL_CONFIG,	cfg_conf_set,		"set <variable> <value>"},

	/* Configuration */	
	{"server",		LEVEL_CONFIG,	cfg_conf_server,	"server add <servertag> <RFC1459|Timestamp|P10|User|BitlBee> <hostname> <service|portnumber> <nickname|none> <localname> <password|none> <identity>"},
	{"channel",		LEVEL_CONFIG,	cfg_conf_channel,	"channel (add <servertag> <channeltag> <name>|link <chantag> <chantag>)"},

	/* Misc commands */
	{"reply",		LEVEL_AUTH,	cfg_misc_reply,		"reply [text]"},
	{"help",		LEVEL_NONE,	cfg_misc_help,		"help"},
	{"quit",		LEVEL_NONE,	cfg_misc_quit,		"quit"},
	{NULL,			LEVEL_NONE,	NULL,			NULL},
};

bool cfg_misc_help(struct cfg_state *cmd, char *args)
{
	int i=0;

	sock_printf(cmd->sock, "201 The following commands are available:\n");
	for (i=0; cfg_cmds[i].cmd; i++)
	{
		/*
		 * If there is no function (thus the command can be ignored)
		 * or the authentication level is not high enough, skip the command
		 */
		if (	cfg_cmds[i].func == NULL ||
			cfg_cmds[i].level > cmd->level) continue;

		sock_printf(cmd->sock, "%-20s %s\n", cfg_cmds[i].cmd, cfg_cmds[i].desc);
	}
	sock_printf(cmd->sock, "202 End of Help\n");
	return true;
}

bool cfg_handlecommand(struct cfg_state *cmd, char *command)
{
	unsigned int	i=0;
	size_t		len;

	/* Ignore the line? */
	if (	strlen(command) == 0 ||
		strncmp(command, "#", 1) == 0 ||
		strncmp(command, "//", 2) == 0)
	{
		/*
		 * We don't output anything for these when we are not bound
		 * to a socket
		 */
		if (cmd->sock != -1) sock_printf(cmd->sock, "200 Ignoring...\n");
		return true;
	}

	for (i=0; cfg_cmds[i].cmd; i++)
	{
		/* If the authentication level is not high enough, skip the command */
		if (cfg_cmds[i].level > cmd->level) continue;

		len = strlen(cfg_cmds[i].cmd);
		if (strncasecmp(cfg_cmds[i].cmd, command, len) != 0 ||
			(command[len] != ' ' && command[len] != '\0')) continue;
		if (cfg_cmds[i].func == NULL)
		{
			sock_printf(cmd->sock, "200 Ignoring...\n");
			return true;
		}
		else return cfg_cmds[i].func(cmd, &command[len+1]);
	}
	sock_printf(cmd->sock, "400 Command unknown '%s'\n", command);
	return false;
}

bool cfg_fromfile(struct cfg_state *cmd, char *filename)
{
	FILE		*file = NULL;
	char		buf[1024];
	size_t		n;
	unsigned int	line = 0;
	bool		ret = true;

	errno = 0;
	file = fopen(filename, "r");
	if (file == NULL)
	{
		sock_printf(cmd->sock, "Couldn't open configuration file %s (%d): %s\n", filename, errno, strerror(errno));
		return false;
	}

	sock_printf(cmd->sock, "Configuring from file %s\n", filename);
	
	/* Walk through the file line by line */
	while (	!cmd->quit &&
		fgets(buf, sizeof(buf), file) == buf)
	{
		/* The last char is -1 ;) */
		n = strlen(buf)-1;

		/* Empty line -> continue */
		if (n <= 0) continue;

		if (buf[n] == '\n') {buf[n] = '\0'; n--;}
		if (buf[n] == '\r') {buf[n] = '\0'; n--;}

		sock_printf(cmd->sock, "Line %u: %s\n", line, buf);

		ret = cfg_handlecommand(cmd, buf);
		if (!ret) break;

		line++;
	}

	sock_printf(cmd->sock, "Configuration file parsing complete\n");

	/* Close the file */
	fclose(file);
	return ret;
}

bool cfg_fromfile_direct(char *file)
{
	struct cfg_state cmd;
	memset(&cmd, 0, sizeof(cmd));

	/* Use dolog() as the output channel */
	cmd.sock = -1;

	/*
	 * This is a console command thus
	 * execute with full priveleges
	 */
	cmd.level = LEVEL_MAXIMUM;
	cmd.quit = false;

	/* Run it */
	return (cfg_fromfile(&cmd, file) && !cmd.quit);
}
