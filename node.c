#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include "ipsum.c"

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
void* sendRoutingUpdates();
int sendRoutingResponse();
int findNextHopInterfaceID(char *NextHop);
int initializeRoutingTable();

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

struct iphdr  {
  uint8_t   tos;      //Type of Service
  uint16_t  tot_len;    //Total Length
  uint16_t  id;       //Identification
  uint16_t  frag_off;   //Fragmentation Offset Field
  uint8_t   ttl;      //Time to Live
  uint8_t   protocol;     //Protocol
  uint16_t  check;      //Checksum
  uint32_t  saddr;      //Source Address
  uint32_t  daddr;      //Destination Address
};

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
  //pthread_create(&tid[1], NULL, &sendRoutingUpdates, NULL);
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
int deserializeRIPPacket (unsigned char * packetBuffer) {
  int i;

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
  return 1;
}

// Sends RIP packets to all interfaces
void* sendRoutingUpdates () {
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
    ptr = malloc(2*sizeof(uint16_t) + num_entries*sizeof(entries));
    ptr = packetBuffer;

    ptr[0] = command >> 8;
    ptr[1] = command;    
    ptr = ptr + 2;

    ptr[0] = num_entries >> 8;
    ptr[1] = num_entries;
    ptr = ptr + 2;

    for(i = 0; i < num_entries; ++i) {
      ptr[0] = entries[i].cost >> 24;
      ptr[1] = entries[i].cost >> 16;
      ptr[2] = entries[i].cost >> 8;
      ptr[3] = entries[i].cost;
      ptr = ptr + 4;

      ptr[0] = entries[i].address >> 24;
      ptr[1] = entries[i].address >> 16;
      ptr[2] = entries[i].address >> 8;
      ptr[3] = entries[i].address;
      ptr = ptr + 4;
    }

    // Test to see that the values are serialized and deserialized correctly
    //deserializeRIPPacket(packetBuffer);

    // Should now send packetBuffer to all immediate neighbors so that they can updateRoutingTables
    struct interface *curr = root;
    while (curr) {
      // Do I use the original socket or create a new socket?
      //sendMessage(curr->port, curr->address, packetBuffer);
      curr = curr->next;
    }

    sleep(5);
  }
  return NULL;
}

// Deserializes IP packets used in forwarding
int deserializeIPPacket(unsigned char * packetBuffer) {

  // The 'd' before variable names indicates deserialized
  uint8_t dtos = 0;
  dtos |= packetBuffer[0];
  printf("Deserialized Tos: %hhu\n", dtos);
  packetBuffer = packetBuffer + 1;

  uint16_t dtot_len = 0;
  dtot_len |= packetBuffer[0] << 8;
  dtot_len |= packetBuffer[1];
  printf("Deserialized tot_len: %hu\n", dtot_len);
  packetBuffer = packetBuffer + 2;

  uint16_t did = 0;
  did |= packetBuffer[0] << 8;
  did |= packetBuffer[1];
  printf("Deserialized id: %hu\n", did);
  packetBuffer = packetBuffer + 2;

  uint16_t dfrag_off = 0;
  dfrag_off |= packetBuffer[0] << 8;
  dfrag_off |= packetBuffer[1];
  printf("Deserialized frag_off: %hu\n", dfrag_off);
  packetBuffer = packetBuffer + 2;

  uint8_t dttl = 0;
  dttl |= packetBuffer[0];
  printf("Deserialized ttl: %hhu\n", dttl);
  packetBuffer = packetBuffer + 1;

  uint8_t dprotocol = 0;
  dprotocol |= packetBuffer[0];
  printf("Deserialized protocol: %hhu\n", dprotocol);
  packetBuffer = packetBuffer + 1;

  uint16_t dcheck = 0;
  dcheck |= packetBuffer[0] << 8;
  dcheck |= packetBuffer[1];
  printf("Deserialized checksum: %hu\n", dcheck);
  packetBuffer = packetBuffer + 2;

  uint32_t dsaddr = 0;
  dsaddr |= packetBuffer[0] << 24;
  dsaddr |= packetBuffer[1] << 16;
  dsaddr |= packetBuffer[2] << 8;
  dsaddr |= packetBuffer[3];
  printf("Deserialized saddr: %u\n", dsaddr);
  packetBuffer = packetBuffer + 4;

  uint32_t ddaddr = 0;
  ddaddr |= packetBuffer[0] << 24;
  ddaddr |= packetBuffer[1] << 16;
  ddaddr |= packetBuffer[2] << 8;
  ddaddr |= packetBuffer[3];
  printf("Deserialized daddr: %u\n", ddaddr);
  packetBuffer = packetBuffer + 4;

  uint32_t temp = 0;
  temp |= packetBuffer[0] << 24;
  temp |= packetBuffer[1] << 16;
  temp |= packetBuffer[2] << 8;
  temp |= packetBuffer[3];
  printf("Deserialized temp: %s\n", itoa(temp));
  packetBuffer = packetBuffer + 4;
  /*
  int i = 0;
  while(packetBuffer[i] != NULL)  {
    itoa(packetBuffer[i], message[i]);
    //printf("char: %s", (char) message[i]);
    packetBuffer = packetBuffer + 1;
  }
  */
  //printf("Deserialized message: %s", packetBuffer[0]);

  return 1;
}

// Serialize ip header info along with message to have enough info for forwarding
void *sendForwardMessage(int sock, char *vip, char *message)  {
  // Serialize packet info into buffer
  unsigned char *packetBuffer, *ptr;
  struct interface *curr = root;
  struct iphdr ip;
  ip.tos = 0;                                                     //Type of Service
  ip.tot_len = htons(20 + sizeof(message) / sizeof(message[0]));  //Total Length (28 bytes for IP and UDP and some data Bytes)
  ip.id = curr->interfaceID;                                      //Identification
  ip.frag_off = 0;                                                //Fragmentation Offset Field
  ip.ttl = MAX_TTL;                                               //Time to Live
  ip.protocol = IPPROTO_UDP;                                      //Protocol
  ip.check = ip_sum(message,2);                                   //Checksum
  ip.saddr = inet_addr(address);                                  //Source Address
  ip.daddr = inet_addr(vip);                                      //Destination Address (vip used to get ports 
                                                                  //in routing tables, so forward vip along)
  
  printf("Tos: %u\n", ip.tos);
  printf("Tot_len: %u\n", ip.tot_len);
  printf("Saddr: %u\n", ip.saddr);
  printf("Daddr: %u\n", ip.daddr);

  packetBuffer = malloc(sizeof(ip) + sizeof(message) / sizeof(message[0]));
  ptr = malloc(sizeof(ip) + sizeof(message) / sizeof(message[0]));
  ptr = packetBuffer;

  // Put iphdr and data into packet buffer
  ptr[0] = ip.tos;
  ptr = ptr + 1;

  ptr[0] = ip.tot_len >> 8;
  ptr[1] = ip.tot_len;    
  ptr = ptr + 2;

  ptr[0] = ip.id >> 8;
  ptr[1] = ip.id;    
  ptr = ptr + 2;

  ptr[0] = ip.frag_off >> 8;
  ptr[1] = ip.frag_off;    
  ptr = ptr + 2;

  ptr[0] = ip.ttl;
  ptr = ptr + 1;

  ptr[0] = ip.protocol;
  ptr = ptr + 1;

  ptr[0] = ip.check >> 8;
  ptr[1] = ip.check;    
  ptr = ptr + 2;
  
  ptr[0] = ip.saddr >> 24;
  ptr[1] = ip.saddr >> 16;
  ptr[2] = ip.saddr >> 8;
  ptr[3] = ip.saddr;
  ptr = ptr + 4;

  ptr[0] = ip.daddr >> 24;
  ptr[1] = ip.daddr >> 16;
  ptr[2] = ip.daddr >> 8;
  ptr[3] = ip.daddr;
  ptr = ptr + 4;
  /*
  memcpy(packetBuffer, (char *)&ip, sizeof(ip));
  memcpy(packetBuffer + sizeof(ip), (char *)&message, sizeof(message) / sizeof(message[0]));
  */

  deserializeIPPacket(packetBuffer);

  sendMessage(curr->port, curr->address, packetBuffer);

  return NULL;
}

// Prints out messages
// Will eventually have to read header and handle forwarding
// Not sure if I should handle RIP packets here or spawn a new thread
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
      //The following should be gibberish if sending happens correctly after IP implementation
      printf("Received Message: %s\n", buf);
    }
  }
  return NULL;
}

// Command line interface for users, supports commands
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
      char *message = strdup(splitMsg);
      sendMessage(sock, vip, message);
    }
    //Test for IP
    else if (strcmp(splitMsg, "sendip") == 0) {
      splitMsg = strtok(NULL, " ");
      char *vip = strdup(splitMsg);
      splitMsg = strtok(NULL, "");
      char *message = strdup(splitMsg);
      sendForwardMessage(sock, vip, message);
    }
    else {
      printf("Invalid Command\n");
    }
    printf("\n");
  }
  close(sock);
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
  // Need to send packets encoded with data. The message should be embedded within and then
  // deserialized at the delivery end point



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

  printInterfaces(root);

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



