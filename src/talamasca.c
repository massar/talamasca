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
 Main Loop, Config & Init
******************************************************/

#include "talamasca.h"

/* Configuration Variables */
struct conf	*g_conf;

/**************************************
  Functions
**************************************/

bool load_config()
{
	if (!cfg_fromfile_direct(g_conf->config_file)) return false;

	/* Check if we have all required variables */
	if (	!g_conf->service_name ||
		!g_conf->service_description ||
		!g_conf->admin_location1 ||
		!g_conf->admin_location2 ||
		!g_conf->admin_email ||
		!g_conf->config_password ||
		g_conf->servers->count < 2)
	{
		dolog(LOG_ERR, "core", "Please configure me completely first, The Talamasca is not a mere toy..\n");
		return false;
	}
	return true;
}

void init()
{
	g_conf = malloc(sizeof(struct conf));
	if (!g_conf)
	{
		dolog(LOG_ERR, "core", "Couldn't init()\n");
		exit(-1);
	}

	/* Clear it, never bad, always good  */
	memset(g_conf, 0, sizeof(*g_conf));
	
	g_conf->boottime		= time(NULL);
	g_conf->config_file		= strdup("/etc/talamasca.conf");

	/* Initialize select */
	FD_ZERO(&g_conf->selectset);

	/* Initialize our list of servers */
	g_conf->servers			= list_new();
	g_conf->servers->del 		= (void(*)(void *))server_destroy;

	/* Initialize our list of users */
	g_conf->users			= list_new();
	g_conf->users->del 		= NULL;
	/* Deleting the servers will remove the users ;) */

	/* Users have to !add themselves */
	g_conf->bitlbee_auto_add	= false;
}

/* Long options */
static struct option const long_options[] = {
	{"config",		no_argument,		NULL, 'c'},
	{"foreground",		no_argument,		NULL, 'f'},
	{"user",		required_argument,	NULL, 'u'},
	{"verbose",		no_argument,		NULL, 'v'},
	{"version",		no_argument,		NULL, 'V'},
	{NULL,			0, NULL, 0},
};

int main(int argc, char *argv[], char *envp[])
{
	int			i, drop_uid = 0, drop_gid = 0, option_index = 0;
	struct passwd		*passwd;
	fd_set			fd_read, fd_except;
	struct timeval		timeout;
	struct listnode		*ln;
	struct server		*server;
	int			len;
	bool			quit = false;

	/* Initialize */
	init();

	/* Handle arguments */
	while ((i = getopt_long(argc, argv, "c:fu:vV", long_options, &option_index)) != EOF)
	{
		switch (i)
		{
		case 0:
			/* Long option */
			break;

		case 'c':
			if (g_conf->config_file) free(g_conf->config_file);
			g_conf->config_file = strdup(optarg);
			break;
		case 'f':
			g_conf->daemonize = false;
			break;
		case 'u':
			passwd = getpwnam(optarg);
			if (passwd)
			{
				drop_uid = passwd->pw_uid;
				drop_gid = passwd->pw_gid;
			}
			else fprintf(stderr, "Couldn't find user %s, aborting\n", optarg);
			break;
		case 'v':
			g_conf->verbose = true;
			break;

		case 'V':
			/*
			 * If you have the intention of editing this message,
			 * then keep at least the Copyright notice in there.
			 * Some people simply want a little respect and credit.
			 */
			printf("Talamasca %s (C) Copyright Jeroen Massar 2004 All Rights Reserved\n", TALAMASCA_VERSION);
			return 0;

		default:
			fprintf(stderr,
				"%s [-c configfile] [-f] [-u username] [-v] [-V]"
				"\n"
				"\n"
				"-c, --config <file>    automatically add everybody who talks\n"
				"-f, --foreground       don't daemonize\n"
				"-u, --user <username>  drop (setuid+setgid) to user after startup\n"
				"-v, --verbose          Verbose Operation\n"
				"-V, --version          Report version and exit\n"
				
				"\n"
				"Report bugs to Jeroen Massar <jeroen@unfix.org>.\n"
				"Also see the website at http://unfix.org/projects/talamasca/\n",
				argv[0]);
			return -1;
		}
	}

	/* Daemonize */
	if (g_conf->daemonize)
	{
		int i = fork();
		if (i < 0)
		{
			fprintf(stderr, "Couldn't fork\n");
			return -1;
		}
		/* Exit the mother fork */
		if (i != 0) return 0;

		/* Child fork */
		setsid();
		/* Cleanup stdin/out/err */
		freopen("/dev/null","r",stdin);
		freopen("/dev/null","w",stdout);
		freopen("/dev/null","w",stderr);
	}

	/* Ignore some signals */
	signal(SIGHUP, SIG_IGN);
	signal(SIGUSR1, SIG_IGN);
	signal(SIGUSR2, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	/* Handle SIGTERM/INT/KILL to cleanup the pid file and exit */
	signal(SIGTERM,	&cleanpid);
	signal(SIGINT,	&cleanpid);
	signal(SIGKILL,	&cleanpid);

	/*
	 * Show our version in the startup logs ;)
	 * If you have the intention of editing this message,
	 * then keep at least the Copyright notice in there.
	 * Some people simply want a little respect and credit.
	 */
	dolog(LOG_INFO, "core", "Talamasca %s (C) Copyright Jeroen Massar 2004 All Rights Reserved\n", TALAMASCA_VERSION);

	/* Save our PID */
	savepid();

	/*
	 * Drop our root priveleges.
	 * We don't need them anymore anyways
	 */
	if (drop_uid != 0) setuid(drop_uid);
	if (drop_gid != 0) setgid(drop_gid);

	/* Load config */
	if (!load_config()) return -1;

	dolog(LOG_DEBUG, "core", "Going into mainloop...\n");

	/* For almost ever */
	while (!g_conf->quit && !quit)
	{
		if (g_conf->numsocks > 0)
		{
			/* What we want to know */
			memcpy(&fd_read, &g_conf->selectset, sizeof(fd_read));
			memcpy(&fd_except, &g_conf->selectset, sizeof(fd_except));

			/* Timeout */
			memset(&timeout, 0, sizeof(timeout));
			timeout.tv_sec = 5;

			i = select(g_conf->hifd+1, &fd_read, NULL, &fd_except, &timeout);
			if (i < 0)
			{
				quit = true;
				if (errno == EINTR) continue;
				dolog(LOG_ERR, "core", "Select failed\n");
				break;
			}
		}
		else
		{
			/* No sockets at all? -> Sleep 5 seconds */
			dolog(LOG_DEBUG, "core", "Sleeping for 5 seconds because we have no sockets at all\n");
			sleep(5);
		}

		LIST_LOOP(g_conf->servers, server, ln)
		{
			if (server->socket == -1)
			{
				server_connect(server);
			}
			else if (FD_ISSET(server->socket, &fd_except))
			{
				server_disconnect(server);
			}
			else if (FD_ISSET(server->socket, &fd_read))
			{
				server_handle(server);
			}
		}
	}

	/* Show the message in the log */
	dolog(LOG_INFO, "core", "Shutdown, thank you for using The Talamasca, remember: we watch and we are always here\n");

	/* Cleanup the lists */
	list_delete(g_conf->users);
	list_delete(g_conf->servers);
	
	/* TODO: free various strings in g_conf */

	/* Free the config memory */
	free(g_conf);
	g_conf = NULL;

	/* We are out of here */
	cleanpid(SIGINT);

	return 0;
}
