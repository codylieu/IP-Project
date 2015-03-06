#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

int handleUserInput();
void* handleReceiveMessages();
int ifconfig();
int routes();
int up(char * interfaceID);
int down(char * interfaceID);
int sendMessage(int sock, char * vip, unsigned char * message);
uint16_t findPort(char *vip);
void* sendRoutingUpdates();
int sendRoutingResponse();
int findNextHopInterfaceID(char *NextHop);
int initializeRoutingTable();

#define MAX_TRANSFER_UNIT (1400)
#define MAX_MSG_LENGTH (512)
#define MAX_BACK_LOG (5)
#define MAX_ROUTES 128 /* maximum size of routing table */
#define MAX_TTL 120 /* time (in seconds) until route expires */
#define MAX_PACKET_BUFFER_SIZE 64000

char *address;
uint16_t port;
struct interface {
  int interfaceID;
  char *address;
  uint16_t port;
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

  // Sets global port and address
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

  // Creates linked list of interfaces
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

  // Initializes routing table with information contained in txt file
  // This should be later encapsulated into routing thread as the first call
  // Or this should encapsulate the routing thread, not sure yet
  initializeRoutingTable();

  pthread_create(&tid[0], NULL, &handleReceiveMessages, NULL);
  pthread_create(&tid[1], NULL, &sendRoutingUpdates, NULL);
  return handleUserInput();

  exit(EXIT_SUCCESS);
}

// Code from the book
void mergeRoute (Route *new) {
  int i;

  for (i = 0; i < numRoutes; ++i) {
    if (strcmp(new->Destination, routingTable[i].Destination) == 0) {
      if (new->cost + 1 < routingTable[i].cost) {
        /* Found a better route: */
        break;
      }
      else if (strcmp(new->NextHop, routingTable[i].NextHop) == 0) {
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

// Code from the book
// After nodes, receive RIP packets from neighbors, call this for each entry to update routing table
void updateRoutingTable (Route *newRoute, int numNewRoutes) {
  int i;
  for (i = 0; i < numNewRoutes; ++i) {
    mergeRoute(&newRoute[i]);
  }
}

// Used when RIP packet is received, will need to put values into a form
// that updateRoutingTable can use 
int deserializePacket (unsigned char * packetBuffer) {
  int i;
  printf("==========DESERIALIZATION START==========\n");
  uint16_t dCommand = 0;
  dCommand |= packetBuffer[0] << 8;
  dCommand |= packetBuffer[1];
  printf("Deserialized Command: %hu\n", dCommand);

  packetBuffer = packetBuffer + 2;

  uint16_t dNum_entries = 0;
  dNum_entries |= packetBuffer[0] << 8;
  dNum_entries |= packetBuffer[1];
  printf("Deserialized Num Entries: %hu\n", dNum_entries);
  packetBuffer = packetBuffer + 2;

  for(i = 0; i < dNum_entries; ++i) {
    uint32_t dCost = 0;
    dCost |= packetBuffer[0] << 24;
    dCost |= packetBuffer[1] << 16;
    dCost |= packetBuffer[2] << 8;
    dCost |= packetBuffer[3];
    packetBuffer = packetBuffer + 4;
    printf("Deserialized Cost %d: %u\n", i, dCost);
    uint32_t dAddress = 0;
    dAddress |= packetBuffer[0] << 24;
    dAddress |= packetBuffer[1] << 16;
    dAddress |= packetBuffer[2] << 8;
    dAddress |= packetBuffer[3];
    packetBuffer = packetBuffer + 4;
    printf("Deserialized Address %d: %u\n", i, dAddress);
  }
  printf("==========DESERIALIZATION END==========\n");
  return 1;
}

// Sends RIP packets to all interfaces
void* sendRoutingUpdates () {
  int sock;
  // if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
  //   perror("simplex-talk: socket");
  //   exit(1);
  // }
  while (1) {
    int i, j;

    // Packet format
    uint16_t command; // command will be 1 for request of routing info and 2 for a response
    uint16_t num_entries; // will not exceed 64 and must be 0 for a request
    struct {
      uint32_t cost; // will not exceed 16 -> define infinity to be 16
      uint32_t address; // IPv4 address
    } entries[num_entries];

    // Hard code in 2 for now, will also have to support request later.
    command = 2;
    num_entries = numRoutes;
    printf("Command: %hu\n", command);
    printf("Num Entries: %hu\n", num_entries);
    for(i = 0; i < num_entries; ++i) {
      entries[i].cost = routingTable[i].cost;
      printf("Cost %d: %u\n", i, entries[i].cost);
      inet_pton(AF_INET, routingTable[i].Destination, &entries[i].address);
      printf("Address %d: %u\n", i, entries[i].address);
    }

    // Serialize packet info into buffer
    unsigned char *packetBuffer, *ptr;
    packetBuffer = malloc(2*sizeof(uint16_t) + num_entries*sizeof(entries));
    // bzero((char *)&packetBuffer, sizeof(packetBuffer));
    ptr = malloc(2*sizeof(uint16_t) + num_entries*sizeof(entries));
    ptr = packetBuffer;

    ptr[0] = command >> 8;
    ptr[1] = command;    
    ptr = ptr + 2;

    ptr[0] = num_entries >> 8;
    ptr[1] = num_entries;
    ptr = ptr + 2;

    for(j = 0; j < num_entries; ++j) {
      ptr[0] = entries[j].cost >> 24;
      ptr[1] = entries[j].cost >> 16;
      ptr[2] = entries[j].cost >> 8;
      ptr[3] = entries[j].cost;
      ptr = ptr + 4;

      ptr[0] = entries[j].address >> 24;
      ptr[1] = entries[j].address >> 16;
      ptr[2] = entries[j].address >> 8;
      ptr[3] = entries[j].address;
      ptr = ptr + 4;
    }

    // Test to see that the values are serialized and deserialized correctly
    // deserializePacket(packetBuffer);

    // Should now send packetBuffer to all immediate neighbors so that they can updateRoutingTables
    struct interface *curr = root;
    while (curr) {
      sendMessage(sock, curr->toAddress, packetBuffer);
      curr = curr->next;
    }
    // close(sock);
    // free(ptr);
    // free(packetBuffer);

    sleep(20);
  }
  return NULL;
}

int send_rip_packets () {
  return 1;
}

// Prints out messages
// Will eventually have to read header and handle forwarding
// Not sure if I should handle RIP packets here or spawn a new thread
void* handleReceiveMessages () {
  struct sockaddr_in sin;
  struct sockaddr_storage from;
  socklen_t fromLen = sizeof(from);
  unsigned char buf[MAX_TRANSFER_UNIT];
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
      int i = 0;
      uint16_t command = 0;
      command |= buf[i] << 8;
      command |= buf[i + 1];
      printf("RECEIVED: Deserialized Command: %hu\n", command);
      if (command == 1 || command == 2) {
        // Format everything to a Route and then call update routes
        i = i + 2;
        uint16_t num_entries = 0;
        num_entries |= buf[i] << 8;
        num_entries |= buf[i + 1];
        i = i + 2;
        printf("RECEIVED: Deserialized Num Entries: %hu\n", num_entries);
        Route newRoutes[num_entries];
        int j;
        for(j = 0; j < num_entries; ++j) {
          Route tempRoute;
          uint32_t cost = 0;
          cost |= buf[i] << 24;
          cost |= buf[i + 1] << 16;
          cost |= buf[i + 2] << 8;
          cost |= buf[i + 3];
          i = i + 4;
          printf("RECEIVED Cost %d: %u\n", j, cost);
          uint32_t address = 0;
          address |= buf[i] << 24;
          address |= buf[i + 1] << 16;
          address |= buf[i + 2] << 8;
          address |= buf[i + 3];
          i = i + 4;
          printf("RECEIVED Address %d: %u\n", j, address);
          
          // Building the Routes
          struct in_addr temp;
          temp.s_addr = address;
          inet_ntop(AF_INET, &temp, tempRoute.Destination, INET_ADDRSTRLEN);
          // tempRoute.NextHop;
          tempRoute.cost = cost;
          tempRoute.TTL = MAX_TTL;
          newRoutes[j] = tempRoute;
        }
        // updateRoutingTable(newRoutes, num_entries);
      }
      else if (command == 0) {
        // Treat it as a signed char *
        int j;
        char deserializedMessage[MAX_PACKET_BUFFER_SIZE];
        for(j = 0; j < fromLen; ++j) {
          deserializedMessage[j] = buf[j + 2];
        }
        printf("Received Message: %s\n", deserializedMessage);
      }
    }
  }
  return NULL;
}

// Command line interface for users, supports commands
int handleUserInput () {
  int sock, recv_len;
  char msg[MAX_MSG_LENGTH], reply[MAX_BACK_LOG * 3];
  char buf[MAX_TRANSFER_UNIT];

  // if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
  //   perror("simplex-talk: socket");
  //   exit(1);
  // }

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
    // Documentation is unclear on whether command is "routes" or "route"
    else if (strcmp(splitMsg, "routes") == 0 ||
             strcmp(splitMsg, "route") == 0) {
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
      char *tempMessage = strdup(splitMsg);

      // Make command 0
      unsigned char *message = malloc(sizeof(int) + sizeof(tempMessage));
      // encode 0 as command
      uint16_t command = 0;
      message[0] = command >> 8;
      message[1] = command;
      
      int i = 0;
      for(i = 0; i < strlen(tempMessage); ++i) {
        message[i + 2] = tempMessage[i];
      }
      sendMessage(sock, vip, message);
    }
    else {
      printf("Invalid Command\n");
    }
    printf("\n");
  }
  // close(sock);
  return 1;
}

// Initialize routing table by going through linked list of interfaces;
int initializeRoutingTable() {
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
  return 1;
}

/* Supported Commands */

// Not sure if this should be expanded later to support RIP packets and forwarding
// It probably should, in which case, we might need to change function signature
int sendMessage (int s, char * vip, unsigned char * message) {
  int sock;
  if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("simplex-talk: socket");
    exit(1);
  }
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
  close(sock);
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

// Remove printing of own from address, currently leave it in for testing
int routes () {
  printf("Response is: %s\n", "routes");
  int i;
  for(i = 0; i < numRoutes; ++i) {
    int id = findNextHopInterfaceID(routingTable[i].NextHop);
    printf("%s %d %d\n", routingTable[i].Destination, id, routingTable[i].cost);
  }
  return 1;
}

// Up and Down will eventually have to handle triggered updates I think
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

/* Helper functions */

// Find associated port given vip
uint16_t findPort (char *vip) {
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