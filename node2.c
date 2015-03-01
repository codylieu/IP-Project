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

char *address;
int port;

struct interface {
  char *address;
  int port;
  char *fromAddress;
  char *toAddress;
  int up;
  struct interface *next;
};

int main(int argc, char ** argv) {
  FILE * fp;
  char * line = NULL;
  size_t len = 0;
  ssize_t read;
  struct interface *root, *curr;
  root = NULL;

  if (argc < 2) {
    printf("No File Indicated\n");
    return 1;
  }

  fp = fopen(argv[1], "r");
  if (fp == NULL)
    exit(EXIT_FAILURE);

  read = getline(&line, &len, fp);
  char *token;
  token = strtok(line, ":");
  if (strcmp(token, "localhost") == 0) {
    address = "127.0.0.1";
  }
  else {
    address = token;
  }
  token = strtok(NULL, ":");
  port = atoi(token);
  printf("Address is %s\n", address);
  printf("Port is %d\n", port);

  while ((read = getline(&line, &len, fp)) != -1) {
    curr = (struct interface *) malloc(sizeof(struct interface));

    char *splitLine;
    splitLine = strtok(line, ":");

    if (strcmp(splitLine, "localhost") == 0) {
      curr->address = strdup("127.0.0.1");
    }
    else {
      curr->address = strdup(splitLine);
    }
    splitLine = strtok(NULL, " ");
    curr->port = atoi(splitLine);
    splitLine = strtok(NULL, " ");
    curr->fromAddress = strdup(splitLine);
    splitLine = strtok(NULL, " ");
    curr->toAddress = strdup(splitLine);
    if (root == NULL) {
      root = curr;
    }
    else {
      struct interface *temp1 = root;
      while(temp1->next != NULL) {
        temp1 = temp1->next;
      }
      temp1->next = curr;
    }
  }
  curr = root;
  while (curr) {
    printf("Address is %s\n", curr->address);
    printf("Port is %d\n", curr->port);
    printf("From Address is %s\n", curr->fromAddress);
    printf("To Address is %s\n", curr->toAddress);
    curr = curr->next;
  }

  fclose(fp);
  if (line)
    free(line);
  exit(EXIT_SUCCESS);
}

int sendMessage () {

}

int receiveMessage () {

}












