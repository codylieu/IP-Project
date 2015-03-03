#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

int server(uint16_t port);
int client(const char * addr, uint16_t port);
int handleUserInput();
void* handleReceiveMessages();
int ifconfig();
int routes();
int up(char * interfaceID);
int down(char * interfaceID);
int sendMessage(int sock, char * vip, char * message);
int findPort(char *vip);
void* sendRoutingRequest();
int sendRoutingResponse();
int findNextHopInterfaceID(char *NextHop);

#define MAX_TRANSFER_UNIT (1400)
#define MAX_MSG_LENGTH (512)
#define MAX_BACK_LOG (5)
#define MAX_ROUTES 128 /* maximum size of routing table */
#define MAX_TTL 120 /* time (in seconds) until route expires */

char *address;
int port;
struct interface {
  int interfaceID;
  char *address;
  int port;
  char *fromAddress;
  char *toAddress;
  int up;
  struct interface *next;
};
struct interface *root;
pthread_t tid[2];

typedef struct {
  char *Destination; /* address of destination */
  char *NextHop; /* address of next hop */
  int cost; /* distance metric */
  u_short TTL; /* time to live */
} Route;

int numRoutes = 0;
Route routingTable[MAX_ROUTES];

int printInterfaces(struct interface * curr);
void mergeRoute(Route *new);
void updateRoutingTable(Route *newRoute, int numNewRoutes);

// Packet format, not sure where this goes yet
// uint16_t command; // 1 for a request of routing info and 2 for a response
// uint16_t num_entries; // will not exceed 64 and must be 0 for a request command
// struct {
//   uint32_t cost;
//   uint32_t address;
// } entries[num_entries];

// Create a make file that compiles with "gcc -pthread -o node node.c" for Ubuntu
int main(int argc, char ** argv) {
  FILE * fp;
  char * line = NULL;
  size_t len = 0;
  ssize_t read;
  struct interface *curr;
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
  token = strtok(NULL, "\n");
  port = atoi(token);
  int interfaceID = 1;

  while ((read = getline(&line, &len, fp)) != -1) {
    curr = (struct interface *) malloc(sizeof(struct interface));
    curr->interfaceID = interfaceID;
    interfaceID++;

    char *splitLine;
    splitLine = strtok(line, ":");

    if (strcmp(splitLine, "localhost") == 0) {
      curr->address = strdup("127.0.0.1");
    }
    else {
      curr->address = strdup(splitLine);
      printf("Address test: %s\n", curr->address);
    }
    splitLine = strtok(NULL, " ");
    curr->port = atoi(splitLine);
    splitLine = strtok(NULL, " ");
    curr->fromAddress = strdup(splitLine);
    splitLine = strtok(NULL, "\n");
    curr->toAddress = strdup(splitLine);
    curr->up = 1;
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

  fclose(fp);
  if (line)
    free(line);

  pthread_create(&tid[0], NULL, &handleReceiveMessages, NULL);
  pthread_create(&tid[1], NULL, &sendRoutingRequest,NULL);
  return handleUserInput();
  exit(EXIT_SUCCESS);
}

void mergeRoute (Route *new) {
  int i;

  for (i = 0; i < numRoutes; ++i) {
    if (new->Destination == routingTable[i].Destination) {
      if (new->cost + 1 < routingTable[i].cost) {
        /* Found a better route: */
        break;
      }
      else if (new->NextHop == routingTable[i].NextHop) {
        /* Metric for current next-hop may have changed: */
        break;
      }
      else {
        /* Route is uninteresting---just ignore it */
        return;
      }
    }
  }
  if (i == numRoutes) {
    /* This is a completely new route; is there room for it? */
    if (numRoutes < MAX_ROUTES) {
      ++numRoutes;
    }
    else {
      /* can't fit this route in table so give up */
      return;
    }
  }
  routingTable[i] = *new;
  /* Reset TTL */
  routingTable[i].TTL = MAX_TTL;
  /* Account for hop to get to next node */
  ++routingTable[i].cost;
}

void updateRoutingTable (Route *newRoute, int numNewRoutes) {
  int i;
  for (i = 0; i < numNewRoutes; ++i) {
    mergeRoute(&newRoute[i]);
  }
}

void* sendRoutingRequest () {
  // Initialize routing table by going through interfaces linked list;
  struct interface *curr = root;
  while (curr) {
    Route *fromRoute = malloc(sizeof(Route));
    fromRoute->Destination = strdup(curr->fromAddress);
    fromRoute->NextHop = strdup(curr->fromAddress);
    fromRoute->cost = 0;
    fromRoute->TTL = MAX_TTL;
    routingTable[numRoutes] = *fromRoute;
    numRoutes++;

    Route *toRoute = malloc(sizeof(Route));
    toRoute->Destination = strdup(curr->toAddress);
    toRoute->NextHop = strdup(curr->toAddress);
    toRoute->cost = 1;
    toRoute->TTL = MAX_TTL;
    routingTable[numRoutes] = *toRoute;
    numRoutes++;
    curr = curr->next;
  }

  // Send requests for updates
  while (1) {

  }
  return NULL;
}

void* handleReceiveMessages () {
  struct sockaddr_in sin, from;
  socklen_t fromLen = sizeof(from);
  char buf[MAX_TRANSFER_UNIT];
  int s;

  bzero((char *)&sin, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = htons(port);

  if ((s = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("simplex-talk: socket");
    exit(1);
  }
  if ((bind(s, (struct sockaddr *)&sin, sizeof(sin))) < 0) {
    perror("simplex-talk: bind");
    exit(1);
  }

  while(1) {
    if (recvfrom(s, buf, MAX_TRANSFER_UNIT, 0, (struct sockaddr*)&from, &fromLen) > 0) {
      printf("Received Message: %s\n", buf);
    }
  }
  return NULL;
}

int handleUserInput () {
  int sock, recv_len;
  char msg[MAX_MSG_LENGTH], reply[MAX_BACK_LOG * 3];
  char buf[MAX_TRANSFER_UNIT];

  if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("simplex-talk: socket");
    exit(1);
  }

  recv_len = 0;
  while (1) {
    fflush(stdin);
    printf("Enter command:\n");
    gets(msg);
    if (strcmp(msg, "") == 0) {
      printf("No Command Entered\n\n");
      continue;
    }
    char *splitMsg;
    splitMsg = strtok(msg, " ");
    if (strcmp(splitMsg, "break") == 0) {
      break;
    }
    else if (strcmp(splitMsg, "ifconfig") == 0) {
      ifconfig();      
    }
    else if (strcmp(splitMsg, "routes") == 0) {
      routes();
    }
    else if (strcmp(splitMsg, "up") == 0) {
      splitMsg = strtok(NULL, " ");
      up(splitMsg);
    }
    else if (strcmp(splitMsg, "down") == 0) {
      splitMsg = strtok(NULL, " ");
      down(splitMsg);
    }
    else if (strcmp(splitMsg, "send") == 0) {
      splitMsg = strtok(NULL, " ");
      char *vip = strdup(splitMsg);
      splitMsg = strtok(NULL, "");
      char *message = strdup(splitMsg);
      sendMessage(sock, vip, message);
    }
    else {
      printf("Invalid Command\n");
    }
    printf("\n");
  }
  close(sock);
  return 1;
}

int sendMessage (int sock, char * vip, char * message) {
  int samePort = 0;
  struct interface *curr = root;
  while (curr) {
    if (strcmp(vip, curr->fromAddress) == 0) {
      samePort = 1;
    }
    curr = curr->next;
  }

  struct sockaddr_in sout;
  socklen_t soutLen = sizeof(sout);
  sout.sin_family = AF_INET;
  sout.sin_addr.s_addr = inet_addr(address);
  if (samePort) {
    sout.sin_port = htons(port);
  }
  else {
    sout.sin_port = htons(findPort(vip));
  }

  if (sendto(sock, message, MAX_TRANSFER_UNIT, 0, (struct sockaddr*)&sout, soutLen) < 0) {
    perror("Send error");
  }
  return 1;
}

int ifconfig () {
  struct interface *curr = root;
  while (curr) {
    char *running = curr-> up == 1 ? "up" : "down";
    printf("%d %s %s\n", curr->interfaceID, curr->fromAddress, running);
    curr = curr->next;
  }
  return 1;
}

// Remove printing of own from address
int routes () {
  printf("Response is: %s\n", "routes");
  int i;
  for(i = 0; i < numRoutes; ++i) {
    int id = findNextHopInterfaceID(routingTable[i].NextHop);
    printf("%s %d %d\n", routingTable[i].Destination, id, routingTable[i].cost);
  }
  return 1;
}

int up (char *interfaceID) {
  if (interfaceID == NULL) {
    printf("No Interface specified\n");
    return 1;
  }
  printf("Interface ID: %d\n", atoi(interfaceID));
  return 1;
}

int down (char *interfaceID) {
  if (interfaceID == NULL) {
    printf("No Interface specified\n");
    return 1;
  }
  printf("Interface ID: %d\n", atoi(interfaceID));
  return 1;
}

int update () {
  return 1;
}

int calculateDistanceVector () {
  return 1;
}

// Find associated port given vip
int findPort (char *vip) {
  struct interface *curr = root;
  while (curr) {
    if (strcmp(curr->toAddress, vip) == 0) {
      return curr->port;
    }
    curr = curr->next;
  }
  return -1;
}

// Find interface ID for Next Hop
int findNextHopInterfaceID (char *NextHop) {
  struct interface *curr = root;
  while (curr) {
    if (strcmp(curr->fromAddress, NextHop) == 0 || 
        strcmp(curr->toAddress, NextHop) == 0) {
      return curr->interfaceID;
    }
    curr = curr->next;
  }
  return -1;
}

// Used for debugging
int printInterfaces (struct interface *curr) {
  while (curr) {
    printf("Address is %s\n", curr->address);
    printf("Port is %d\n", curr->port);
    printf("From Address is %s\n", curr->fromAddress);
    printf("To Address is %s\n", curr->toAddress);
    curr = curr->next;
  }
  return 1;
}



