#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>

int server(uint16_t port);
int client(const char * addr, uint16_t port);

#define MAX_MSG_LENGTH (512)
#define MAX_BACK_LOG (5)

int main(int argc, char ** argv)
{
  if (argc < 3) {
    printf("Command should be: myprog s <port> or myprog c <port> <address>\n");
    return 1;
  }
  int port = atoi(argv[2]);
  if (port < 1024 || port > 65535) {
    printf("Port number should be equal to or larger than 1024 and smaller than 65535\n");
    return 1;
  }
  if (argv[1][0] == 'c') {
    if(argv[3]==NULL){
      printf("NO IP address is given\n");
      return 1;
    }
    return client(argv[3], port);
  } else if (argv[1][0] == 's') {
    return server(port);
  } else {
    printf("unknown command type %s\nCommand should be: myprog s <port> or myprog c <port> <address>", argv[1]);
    return 1;
  }
  return 0;
}

int client(const char * addr, uint16_t port)
{
  int sock;
  struct sockaddr_in server_addr;
  char msg[MAX_MSG_LENGTH], reply[MAX_MSG_LENGTH*3];

  if ((sock = socket(AF_INET, SOCK_STREAM/* use tcp */, 0)) < 0) {
    perror("Create socket error:");
    return 1;
  }

  printf("Socket created\n");
  server_addr.sin_addr.s_addr = inet_addr(addr);
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);

  if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    perror("Connect error:");
    return 1;
  }

  printf("Connected to server %s:%d\n", addr, port);

  int recv_len = 0;
  while (1) {
    fflush(stdin);
    printf("Enter message: \n");
    gets(msg);
    if (send(sock, msg, MAX_MSG_LENGTH, 0) < 0) {
      perror("Send error:");
      return 1;
    }
    recv_len = read(sock, reply, MAX_MSG_LENGTH*3);
    if (recv_len < 0) {
      perror("Recv error:");
      return 1;
    }
    reply[recv_len] = 0;
    printf("Server reply:\n%s\n", reply);
    memset(reply, 0, sizeof(reply));
  }
  close(sock);
  return 0;
}

int server(uint16_t port)
{
  /*
    add your code here
  */
  struct sockaddr_in sin;
  char buf[MAX_MSG_LENGTH];
  int len;
  int s, new_s;

  bzero((char *)&sin, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = htons(port);
  if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("simplex-talk: socket");
    exit(1);
  }
  if ((bind(s, (struct sockaddr *)&sin, sizeof(sin))) < 0) {
    perror("simplex-talk: bind");
    exit(1);
  }
  listen(s, MAX_BACK_LOG);
  while(1) {
    if ((new_s = accept(s, (struct sockaddr *)&sin, &len)) < 0) {
      perror("simplex-talk: accept");
      exit(1);
    }
    while (len = recv(new_s, buf, sizeof(buf), 0)) {
      //fputs(buf, stdout);
      char bufResponse[3 * MAX_MSG_LENGTH];
      strcpy(bufResponse, buf);
      strcat(bufResponse, buf);
      strcat(bufResponse, buf);
      send(new_s, bufResponse, sizeof(bufResponse), 0);
    }
    close(new_s);
  }
  return 0;
}