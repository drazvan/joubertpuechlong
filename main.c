#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "game.h"
#include "http.h"

#define max(x, y) (x > y ? x : y)

int game_server_port = 0;			// The port on which we listen for incoming game requests

char opponent_ip[16];				// The opponents ip (when we join)
int opponent_port = 0;				// The opponents port (when we join)


int opponent_sock=-1;				// The opponent sock

// TODO: (2) define game states here
#define NONE 0
#define FINISHED 100

// Structure with the parameters requiered for API calls
struct param
{
	// The requested command
	// !!!: CHANGED (1) fill in the rest
	char* command;
	char* name;
	char* ip;
	char* mdp;	
	int port;
	char* opponent;
	int id_game;
	char* winner;
	char* board;
	char* status;

};

// The structure that will be used throughout the code to send requests
struct param req;

// Creates and connects a socket
int create_socket(int protocol, char* ip, int port)
{
	struct sockaddr_in address;
	int sock = socket(AF_INET, protocol, 0);
	memset(&address, 0, sizeof(address));
	address.sin_addr.s_addr = inet_addr(ip);
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	if ((sock == -1) || (connect(sock, (struct sockaddr *) &address, sizeof(address)) == -1))
	{
		perror("connection error");
	}
	return sock;
}

// Sends a request to the public server
char* send_request(struct param params)
{
	int socketfd;				// socket to connectig4.appspot.com:80
	char data[255];				// data part of the request
	char response[1000];		// the whole response from the server
	int bytes_read;				// the length of the response

	socketfd = create_socket(SOCK_STREAM, "209.85.148.141", 80);
	sprintf(data, "command=%s&name=%s&mdp=%s&ip=%s&port=%d&opponent=%s&game=%d&status=%s",
			params.command, params.name, params.mdp, params.ip, params.port, params.opponent,
			params.id_game, params.status);

    if(params.board!= NULL)
		sprintf(data,"%s&board=%s", data, params.board);

	if( params.winner != NULL)
		sprintf(data,"%s&winner=%s", data, params.winner);

	char buffer[1000];
	sprintf(buffer, "POST /api HTTP/1.1\nHost: connectig4.appspot.com\nContent-Type: application/x-www-form-urlencoded\nContent-Length: %d\n\n%s",
			strlen(data), data);

	send(socketfd, buffer, sizeof(buffer), 0);
	bytes_read = recv(socketfd, response, sizeof(response), 0);

	// Extracts only th epart between --- and ---
	int count = 0, start = 0, end = 0;

	// Find the begining of the server's message (marked by "---")
	for (; start < bytes_read && count < 3; start++)
	{
		if (response[start] == '-')
			count++;
		else
			count = 0;
	}

	count = 0;
	end = start + 1;

	for (; end < bytes_read && count < 3; end++)
	{
		if (response[end] == '-')
			count++;
		else
			count = 0;
	}

	// Truncate the original str to the begining of the last ---
	response[end - 3] = 0;

	char* res = malloc(1000);
	strcpy(res, response + start + 1);

	return res;
}

// Extracts the local ip.
// The best solution is to create a UDP socket to some external ip.
void get_local_ip(char* str_ip)
{
	int sock = create_socket(SOCK_DGRAM, "8.8.8.8", 43);
	struct sockaddr_in name;
    int namelen = sizeof(name);
    getsockname(sock, (struct sockaddr*) &name, &namelen);

    char* p = inet_ntoa(name.sin_addr);
	strcpy(str_ip, p);
    close(sock);
}

// Starts the game server (where the other players will connect)
int start_game_server()
{
	int game_sock = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in game_in;
	struct sockaddr_in game_addr;
	int game_in_len = sizeof(game_in);

	memset(&game_addr, 0, sizeof(game_addr));

	game_addr.sin_addr.s_addr = INADDR_ANY;
	game_addr.sin_family = AF_INET;
	game_addr.sin_port = htons(0);
	bind(game_sock, (struct sockaddr*) &game_addr, sizeof(game_addr));
	listen(game_sock, 1);

	getsockname(game_sock, (const struct sockaddr*) &game_in, &game_in_len);
	game_server_port = ntohs(game_in.sin_port);

	return game_sock;
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		printf("Insufficient arguments. Usage %s 'name' 'mdp'.\n", argv[0]);
		return;
	}

	int game_server_sock = start_game_server();	// socket were we listen for new connections from other players
	
	memset(&req, 0, sizeof(struct param));

	req.command = "connect";
	req.name = argv[1];
	req.mdp = argv[2];
	req.ip = (char *)malloc(sizeof(char)*16);
	get_local_ip(req.ip);
	req.id_game = -1;
	int play = -1;
	req.port = game_server_port;
	char* response = send_request(req);

	if (strncmp(response, "OK", 2) != 0)
	{
		printf(response);
		return;
	}
	free(response);
	fd_set fds;

	printf ("connect4> ");
	fflush(stdout);

	int n, retval;
	struct timeval tv;

	/* Wait up to five seconds. */
    tv.tv_sec = 5;
    tv.tv_usec = 0;

	char command[200];

	while(1)
	{
		FD_ZERO(&fds);
		FD_SET(0, &fds);
		FD_SET(game_server_sock, &fds);
		if (opponent_sock != -1)
		{
			FD_SET(opponent_sock, &fds);
		}

		// Wait for something to happen or for 5 seconds to pass
		retval = select(max(game_server_sock,opponent_sock) + 1, &fds, NULL, NULL, &tv);

		// if it's a timeout
		if (retval == 0)
		{
			req.command="ping";
			send_request(req);
			tv.tv_sec = 5;
		} else
		{
			// If we have a new game connection
			if (FD_ISSET(game_server_sock, &fds))
			{
				struct sockaddr_in address;
				address.sin_addr.s_addr = INADDR_ANY;
				address.sin_family = AF_INET;
				address.sin_port = htons(0);
				socklen_t address_len = sizeof(address);
				opponent_sock=accept(game_server_sock,  (struct sockaddr*) &address, &address_len);

				if (opponent_sock<0)
				{
					perror("Connection error with opponent sock");
				}

				char buffer[1000];
				if(recv(opponent_sock, buffer, 1000, 0) > 0)
				{
					if (buffer[0] == '0')
					{
						printf("aa\n");
						char* opponent = (char *) malloc(sizeof(char)*strlen(buffer));
						strcpy(opponent, buffer + 1);
						req.opponent = opponent;
						
						char startmessage[2] = "10";
						if (send(opponent_sock,startmessage,strlen(startmessage),0)!=strlen(startmessage))
						{
							perror("Send BMessage error");
						}
					}
				}
				req.command = "update";
				req.status="PLAYING";
				send_request(req);
				play = 0;
				init_board();
				printf("\n");
				show_board();

			}

			if (opponent_sock !=-1)
			{
				if (FD_ISSET(opponent_sock, &fds))
				{
					char buffer[1000];
					if(recv(opponent_sock, buffer, 1000, 0) > 0)
					{
						int column = atoi(buffer + 1);
						if (req.id_game >-1)
						{
							make_move(column, 'X');
							printf("\n");
							show_board();
							req.board = (char *) malloc(sizeof(char)* 42);
							fill_board(req.board);
							req.command = "update";
							req.status="PLAYING";
							int isWinner = is_winner();
							if (isWinner>0)
							{
								char winMessage[3];
								if (isWinner==1)
								{
								    req.status="FINISHED";
									req.winner=req.name;
									sprintf(winMessage,"30");
								}
								if (isWinner==2)
								{
								    req.status="FINISHED";
									req.winner=req.opponent;
									sprintf(winMessage,"31");
								}

								if (send(opponent_sock,winMessage,strlen(winMessage),0)!=strlen(winMessage))
								{
									perror("Send win message error");
								}
							}
							send_request(req);
						}
						else
						{
							// Message beginning by 3: Win message
							if (buffer[0]== '3')
							{
								show_board();
								if (buffer[1] == '1')
									printf("Victory \n");
								else
									printf("Defeat \n");
							}
							else
							{

								make_move(column, 'O');
								printf("\n");
								show_board();
							}
						}
						play = 0;
					}
				}
			}
			if (FD_ISSET(0, &fds))
			{
				fgets(command, 200, stdin);
				// list
				if (strncmp(command, "list", 4) == 0)
				{
					req.command = "list";
					char* response = send_request(req);
					printf ("%s\n", response);

					free(response);
				} else
				// create
				if (strncmp(command, "create", 6) == 0)
				{
					req.command = "create";
					char* response = send_request(req);
					printf ("%s\n", response);

					req.id_game = atoi(response + 3);
					//
					free(response);
				} else
				// join
				if (strncmp(command, "join", 4) == 0)
				{			
					req.command = "join";
					command[strlen(command) - 1] = 0;

					char* opponent = (char *) malloc(sizeof(char)*strlen(command)-4);
					strcpy(opponent, command + 5);
					req.opponent = opponent;

					char* response = send_request(req);
					printf ("%s\n", response);

					char *ip_end = response + 3;
					while (*ip_end != ',')
						ip_end++;
					*ip_end = 0;

					strcpy(opponent_ip, response + 3);
					opponent_port = atoi(ip_end + 2);

					printf("opponent IP address is '%s':%d\n", opponent_ip, opponent_port);

					opponent_sock = create_socket(SOCK_STREAM, opponent_ip, opponent_port);
					char HMessage[1000];
					sprintf(HMessage, "0%s",req.opponent);
					if (send(opponent_sock,HMessage,strlen(msg),0)!=strlen(HMessage))
					{
						perror("Send Hello Message error");
					}
					listen(opponent_sock, 1);
					char startMessage[1000];
					if(recv(opponent_sock, startMessage, 1000, 0) > 0)
					{
						if (startMessage[1] == '1')
							play = 0;

					}
					init_board();
					printf("\n");
					show_board();
					free(response);
					free(opponent);

				} else
				if (strncmp(command, "move", 4) == 0)
				{
					if (play == -1)
					{
						printf("No opponent wait \n");
					}
					else
					{
						int column = atoi(command + 5);
						char movemsg[1000];
						sprintf(movemsg, "2%d", column);
						if (req.id_game >-1)
						{
							make_move(column, 'O');
							printf("\n");
							show_board();
							req.board = (char *) malloc(sizeof(char)* 42);
							fill_board(req.board);
							req.command = "update";
							req.status="PLAYING";
							int isWinner = is_winner();
							if (isWinner>0)
							{
								char winMsg[3];
								if (isWinner==1)
								{
								    req.status="FINISHED";
									req.winner=req.name;
									sprintf(winmsg,"30");
								}
								if (isWinner==2)
								{
								    req.status="FINISHED";
									req.winner=req.opponent;
									sprintf(winmsg,"31");
								}
								if (send(opponent_sock,winmsg,strlen(winmsg),0)!=strlen(winmsg))
								{
									perror("Send win message error");
								}
							}
							send_request(req);
						}
						else
						{
							make_move(column, 'X');
							printf("\n");
							show_board();
						}
						if (send(opponent_sock,movemsg,strlen(movemsg),0)!=strlen(movemsg))
						{
							perror("Send move message error");
						}
						play = -1;
					}

				} else
				// exit
				if (strncmp(command, "exit", 4) == 0)
				{
					return 0;
				} else
				// help
				if (strncmp(command, "help", 4) == 0)
				{
					printf("Available commands:\n");
					printf(" - list: shows the list of current waiting games\n");
					printf(" - create: create a new game and wait for players\n");
					printf(" - join 'player': joins the game created by the given player\n");
					printf(" - move 'column': make a game move\n");
					printf(" - help: shows this help\n");
					printf(" - exit\n");
				} else
				{
					printf("unknown command %s\n", command);
				}

				// Show a new prompt
				printf ("connect4> ");
				fflush(stdout);
			}
		}
	}

	return 0;
}
