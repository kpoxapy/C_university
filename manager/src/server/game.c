#include <sys/socket.h>
#include "game.h"
#include "mes.h"
#include "server.h"

int checkBufferOnCommand(Player *);

Game * newGame()
{
	Game * g;

	if ((g = (Game *)malloc(sizeof(Game))) == NULL)
		return NULL;

	g->num = 0;
	g->gid = 0;
	g->nPlayers = 0;
	g->players = NULL;

	return g;
}

void remGame(Game * g)
{
	if (g == NULL)
		return;

	while (g->players != NULL)
		freePlayer(g->players->player);

	free(g);
}

Player * newPlayer(int fd)
{
	Player * p;

	if ((p = (Player *)malloc(sizeof(Player))) == NULL)
		return NULL;

	p->fd = fd;
	p->game = NULL;
	p->buf = newBuffer();
	p->pid = 0;
	p->nick = NULL;
	p->dummynick = NULL;
	p->adm = 0;

	return p;
}

void remPlayer(Player * p)
{
	if (p == NULL)
		return;

	if (p->fd >= 0)
	{
		shutdown(p->fd, SHUT_RDWR);
		close(p->fd);
	}

	clearBuffer(p->buf);
	free(p->buf);

	if (p->nick != NULL)
		free(p->nick);

	if (p->dummynick != NULL)
		free(p->dummynick);

	free(p);
}

char * getNickname(Player * p)
{
	if (p == NULL)
		return NULL;

	if (p->nick != NULL)
		return p->nick;

	if (p->dummynick == NULL)
	{
		char * str, *nstr;
		int size = 8;
		int n;

		str = (char *)malloc(size);
		if (str == NULL)
			return NULL;

		while (1)
		{
			n = snprintf(str, size, "Player %d", p->pid);

			if (n > -1 && n < size)
			{
				if (size - n > 1)
					str = realloc(str, n + 1);
				break;
			}
			else if (n > -1)
				size = n + 1;
			else
			{
				free(str);
				return NULL;
			}

			if ((nstr = (char *)realloc(str, size)) == NULL)
			{
				free(str);
				return NULL;
			}
			else
				str = nstr;
		}

		p->dummynick = str;
	}
	
	return p->dummynick;
}

void fetchData(Player * p)
{
	int n;
	char buf[8];

	if ((n = read(p->fd, buf, 7)) > 0)
	{
		char * c;
		char * nick = getNickname(p);

		buf[n] = '\0';

		debug("Getted string from %s with n=%d: ", nick, n);
		for (c = buf; (c - buf) < n; c++)
			debug("%hhu ", *c);
		debug("\n");

		addnStr(p->buf, buf, n);
	}
	else if (n == 0)
	{
		Message mes;
		if (p->game == NULL)
		{
			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_GAME;
			mes.rcvr.game = NULL;
			mes.type = MEST_PLAYER_LEAVES_HALL;
			mes.len = 0;
			mes.data = NULL;
		}
		else
		{
			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_GAME;
			mes.rcvr.game = p->game;
			mes.type = MEST_PLAYER_LEAVE_GAME;
			mes.len = 0;
			mes.data = NULL;
		}
		sendMessage(&mes);
		mes.sndr_t = O_PLAYER;
		mes.sndr.player = p;
		mes.rcvr_t = O_SERVER;
		mes.type = MEST_PLAYER_LEAVE_SERVER;
		mes.len = 0;
		mes.data = NULL;
		sendMessage(&mes);
	}
	else
		merror("read on fd=%d\n", p->fd);
}

void checkPlayerOnCommand(Player * p)
{
	while(checkBufferOnCommand(p))
		;
}

/* check on command and return 1 if command got
 * or 0 if didn't */
int checkBufferOnCommand(Player * p)
{
	int len = p->buf->count;
	int slen = 0;
	char * buf = flushBuffer(p->buf);
	char * sek = buf;
	Message mes;
	int * pnum;
	char * c;

	memset(&mes, 0, sizeof(mes));

	debug("Analyzing string with len=%d: ", len);
	for (c = buf; (c - buf) < len; c++)
		debug("%hhu ", *c);
	debug("\n");

	if (len == 0)
	{
		free(buf);
		return 0;
	}

	switch (*sek++)
	{
		case 1: /* get */
			if (len > 1)
				addnStr(p->buf, sek, len - 1);
			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_GAME;
			mes.rcvr.game = p->game;
			mes.type = MEST_COMMAND_GET;
			mes.len = 0;
			mes.data = NULL;
			sendMessage(&mes);
			free(buf);
			return 1;

		case 2: /* set N */
			if (len < 5)
			{
				addnStr(p->buf, buf, len);
				free(buf);
				return 0;
			}
			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_GAME;
			mes.rcvr.game = p->game;
			mes.type = MEST_COMMAND_SET;
			mes.len = 4;
			pnum = (int *)malloc(mes.len);
			*pnum = ntohl(*(uint32_t *)sek);
			mes.data = pnum;
			sendMessage(&mes);
			if (len > 5)
				addnStr(p->buf, sek+4, len - 5);
			free(buf);
			return 1;

		case 3: /* join N */
			if (len < 5)
			{
				addnStr(p->buf, buf, len);
				free(buf);
				return 0;
			}
			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_SERVER;
			mes.type = MEST_COMMAND_JOIN;
			mes.len = 4;
			pnum = (int *)malloc(mes.len);
			*pnum = ntohl(*(uint32_t *)sek);
			mes.data = pnum;
			sendMessage(&mes);
			if (len > 5)
				addnStr(p->buf, sek+4, len - 5);
			free(buf);
			return 1;

		case 4: /* leave */
			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_GAME;
			mes.rcvr.game = p->game;
			mes.type = MEST_COMMAND_LEAVE;
			mes.len = 0;
			mes.data = NULL;
			sendMessage(&mes);
			free(buf);
			return 1;

		case 5: /* nick len NICKNAME */
			if (len < 5)
			{
				addnStr(p->buf, buf, len);
				free(buf);
				return 0;
			}

			slen = ntohl(*(uint32_t *)sek);
			if (len < (5 + slen))
			{
				addnStr(p->buf, buf, len);
				free(buf);
				return 0;
			}

			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_SERVER;
			mes.type = MEST_COMMAND_NICK;
			mes.len = slen + 1;
			c = (char *)malloc(mes.len);
			memcpy(c, sek + 4, slen);
			c[slen] = '\0';
			mes.data = c;
			sendMessage(&mes);

			if (len > (5 + slen))
				addnStr(p->buf, sek+4+slen, len - 5 - slen);
			free(buf);
			return 1;

		case 6: /* adm len PASSWORD */
			if (len < 5)
			{
				addnStr(p->buf, buf, len);
				free(buf);
				return 0;
			}

			slen = ntohl(*(uint32_t *)sek);
			if (len < (5 + slen))
			{
				addnStr(p->buf, buf, len);
				free(buf);
				return 0;
			}

			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_SERVER;
			mes.type = MEST_COMMAND_ADM;
			mes.len = slen + 1;
			c = (char *)malloc(mes.len);
			memcpy(c, sek + 4, slen);
			c[slen] = '\0';
			mes.data = c;
			sendMessage(&mes);

			if (len > (5 + slen))
				addnStr(p->buf, sek+4+slen, len - 5 - slen);
			free(buf);
			return 1;

		case 7: /* games */
			if (len > 1)
				addnStr(p->buf, sek, len - 1);
			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_SERVER;
			mes.type = MEST_COMMAND_GAMES;
			mes.len = 0;
			mes.data = NULL;
			sendMessage(&mes);
			free(buf);
			return 1;

		case 8: /* players N */
			if (len < 5)
			{
				addnStr(p->buf, buf, len);
				free(buf);
				return 0;
			}
			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_SERVER;
			mes.type = MEST_COMMAND_PLAYERS;
			mes.len = 4;
			pnum = (int *)malloc(mes.len);
			*pnum = ntohl(*(uint32_t *)sek);
			mes.data = pnum;
			sendMessage(&mes);
			if (len > 5)
				addnStr(p->buf, sek+4, len - 5);
			free(buf);
			return 1;

		case 9: /* creategame */
			if (len > 1)
				addnStr(p->buf, sek, len - 1);
			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_SERVER;
			mes.type = MEST_COMMAND_CREATEGAME;
			mes.len = 0;
			mes.data = NULL;
			sendMessage(&mes);
			free(buf);
			return 1;

		case 10: /* deletegame N */
			if (len < 5)
			{
				addnStr(p->buf, buf, len);
				free(buf);
				return 0;
			}
			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_SERVER;
			mes.type = MEST_COMMAND_DELETEGAME;
			mes.len = 4;
			pnum = (int *)malloc(mes.len);
			*pnum = ntohl(*(uint32_t *)sek);
			mes.data = pnum;
			sendMessage(&mes);
			if (len > 5)
				addnStr(p->buf, sek+4, len - 5);
			free(buf);
			return 1;

		case 11: /* player N */
			if (len < 5)
			{
				addnStr(p->buf, buf, len);
				free(buf);
				return 0;
			}
			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_SERVER;
			mes.type = MEST_COMMAND_PLAYER;
			mes.len = 4;
			pnum = (int *)malloc(mes.len);
			*pnum = ntohl(*(uint32_t *)sek);
			mes.data = pnum;
			sendMessage(&mes);
			if (len > 5)
				addnStr(p->buf, sek+4, len - 5);
			free(buf);
			return 1;

		default:
			mes.sndr_t = O_PLAYER;
			mes.sndr.player = p;
			mes.rcvr_t = O_SERVER;
			mes.type = MEST_COMMAND_UNKNOWN;
			mes.len = len;
			mes.data = buf;
			sendMessage(&mes);
			return 0;
	}
}
