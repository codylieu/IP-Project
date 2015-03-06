#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include "ipsum.c"

void handleUserInput();
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
void *packageData(int sock, char *vip, unsigned char *message, int protocol);
int checkDestinationAddress(uint32_t ddaddr);
char *findSourceVip();
void send_rip_packets(uint16_t command, int interfaceID, char *fromAddress, char *toAddress);

#define MAX_TRANSFER_UNIT (1400)
#define MAX_MSG_LENGTH (512)
#define MAX_BACK_LOG (5)
#define MAX_ROUTES 128 /* maximum size of routing table */
#define MAX_TTL 120 /* time (in seconds) until route expires */
#define MAX_PACKET_BUFFER_SIZE 64000
#define MAX_COST 16; /* Define 16 to be infinity */

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

struct iphdr ipReceived;

int numRoutes = 0;
Route routingTable[MAX_ROUTES];
int sock2;
int printInterfaces(struct interface * curr);
void mergeRoute(Route *new);
void updateRoutingTable(Route *newRoute, int numNewRoutes);

struct deserializedTuple {
  struct iphdr ipReceived;
  unsigned char * deserializedPacketPtr;
};

// Create a make file that compiles with "gcc -pthread -o node node.c" for Ubuntu
int main(int argc, char ** argv) {
  FILE * fp;
  char * line = NULL;
  size_t len = 0;
  ssize_t read;
  struct interface *builder;
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
    builder = (struct interface *) malloc(sizeof(struct interface));
    builder->interfaceID = interfaceID;
    interfaceID++;

    char *splitLine;
    splitLine = strtok(line, ":");

    if (strcmp(splitLine, "localhost") == 0) {
      builder->address = strdup("127.0.0.1");
    }
    else {
      builder->address = strdup(splitLine);
      printf("Address test: %s\n", builder->address);
    }
    splitLine = strtok(NULL, " ");
    builder->port = atoi(splitLine);
    splitLine = strtok(NULL, " ");
    builder->fromAddress = strdup(splitLine);
    splitLine = strtok(NULL, "\n");
    builder->toAddress = strdup(splitLine);
    builder->up = 1;
    if (root == NULL) {
      root = builder;
    }
    else {
      struct interface *temp1 = root;
      while(temp1->next != NULL) {
        temp1 = temp1->next;
      }
      temp1->next = builder;
    }
  }

  fclose(fp);
  if (line)
    free(line);

  if ((sock2 = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("simplex-talk: socket");
    exit(1);
  }

  initializeRoutingTable();
  // Once nodes come online, send request to each interface
  struct interface *iter = root;
  while (iter) {
    send_rip_packets(1, iter->interfaceID, strdup(iter->fromAddress), strdup(iter->toAddress));
    iter = iter->next;
  }

  pthread_create(&tid[0], NULL, &handleReceiveMessages, NULL);
  // pthread_create(&tid[1], NULL, &sendRoutingUpdates, NULL);
  handleUserInput();
  // One problem that could happen later is that I only free one instance of builder,
  // but multiple builders were used to create the linkedlist
  free(builder);
  close(sock2);
  exit(EXIT_SUCCESS);
}

// Code from the book
void mergeRoute (Route *new) {
  int i;
  // printf("NEW DEST: %s\n", new->Destination);
  for (i = 0; i < numRoutes; ++i) {
    // printf("new Dest: %s\n", new->Destination);
    // printf("rt Dest: %s\n", routingTable[i].Destination);
    // printf("Are new and rt dest equql: %d\n", strcmp(new->Destination, routingTable[i].Destination));
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

// Sends RIP packets to all neighbors (interfaces)
void* sendRoutingUpdates () {
  while (1) {
    int i, j;

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
    unsigned char *packetBuffer, *tempPtr;
    packetBuffer = malloc(sizeof(struct iphdr) + 2*sizeof(uint16_t) + 2*num_entries*sizeof(uint32_t));
    tempPtr = packetBuffer;

    // ptr = serializeIp(vip, 200, size, ptr);
    struct interface *curr = root;

    // Need to get copy of the payload in order for checksum to work properly
    // Getting a copy of the payload
    unsigned char* ripbuf, *ripptr;
    ripbuf = malloc(2*sizeof(uint16_t) + 2*num_entries*sizeof(uint32_t));
    ripptr = ripbuf;
    // Adding entries
    ripptr[0] = command >> 8;
    ripptr[1] = command;    
    ripptr = ripptr + 2;
   
    ripptr[0] = num_entries >> 8;
    ripptr[1] = num_entries;
    ripptr = ripptr + 2;

    for(j = 0; j < num_entries; ++j) {
      ripptr[0] = entries[j].cost >> 24;
      ripptr[1] = entries[j].cost >> 16;
      ripptr[2] = entries[j].cost >> 8;
      ripptr[3] = entries[j].cost;
      ripptr = ripptr + 4;
   
      ripptr[0] = entries[j].address >> 24;
      ripptr[1] = entries[j].address >> 16;
      ripptr[2] = entries[j].address >> 8;
      ripptr[3] = entries[j].address;
      ripptr = ripptr + 4;
    }

    while(curr) {
      struct iphdr ip;
      ip.tos = 0;                                 //Type of Service
      ip.tot_len = sizeof(struct iphdr) + 2*sizeof(uint16_t) + num_entries*sizeof(entries);                            //Total Length (28 bytes for IP and UDP and some data Bytes)
      ip.id = curr->interfaceID;                  //Identification
      ip.frag_off = 0;                            //Fragmentation Offset Field
      ip.ttl = MAX_TTL;                           //Time to Live
      ip.protocol = 200;                          //Protocol
      ip.check = ip_sum(ripbuf,2);                               //Checksum
      ip.saddr = inet_addr(curr->fromAddress);    //Source Address
      ip.daddr = inet_addr(curr->toAddress);      //Destination Address (vip used to get ports in routing tables, so forward vip along)

      tempPtr[0] = ip.tos;
      tempPtr = tempPtr + 1;

      tempPtr[0] = ip.tot_len >> 8;
      tempPtr[1] = ip.tot_len;    
      tempPtr = tempPtr + 2;

      tempPtr[0] = ip.id >> 8;
      tempPtr[1] = ip.id;
      tempPtr = tempPtr + 2;

      tempPtr[0] = ip.frag_off >> 8;
      tempPtr[1] = ip.frag_off;    
      tempPtr = tempPtr + 2;

      tempPtr[0] = ip.ttl;
      tempPtr = tempPtr + 1;

      tempPtr[0] = ip.protocol;
      tempPtr = tempPtr + 1;

      tempPtr[0] = ip.check >> 8;
      tempPtr[1] = ip.check;    
      tempPtr = tempPtr + 2;
      
      tempPtr[0] = ip.saddr >> 24;
      tempPtr[1] = ip.saddr >> 16;
      tempPtr[2] = ip.saddr >> 8;
      tempPtr[3] = ip.saddr;
      tempPtr = tempPtr + 4;

      tempPtr[0] = ip.daddr >> 24;
      tempPtr[1] = ip.daddr >> 16;
      tempPtr[2] = ip.daddr >> 8;
      tempPtr[3] = ip.daddr;
      tempPtr = tempPtr + 4;

      tempPtr[0] = command >> 8;
      tempPtr[1] = command;    
      tempPtr = tempPtr + 2;
   
      tempPtr[0] = num_entries >> 8;
      tempPtr[1] = num_entries;
      tempPtr = tempPtr + 2;

      for(j = 0; j < num_entries; ++j) {
        tempPtr[0] = entries[j].cost >> 24;
        tempPtr[1] = entries[j].cost >> 16;
        tempPtr[2] = entries[j].cost >> 8;
        tempPtr[3] = entries[j].cost;
        tempPtr = tempPtr + 4;
   
        tempPtr[0] = entries[j].address >> 24;
        tempPtr[1] = entries[j].address >> 16;
        tempPtr[2] = entries[j].address >> 8;
        tempPtr[3] = entries[j].address;
        uint32_t address = 0;
        tempPtr = tempPtr + 4;
      }
      sendMessage(0, curr->toAddress, packetBuffer);
      curr = curr->next;
    }
    free(ripbuf);
    free(packetBuffer);
    sleep(10);
  }
  return NULL;
}

// deserializes IP packets used in forwarding
struct deserializedTuple deserializeIPPacket(unsigned char * packetBuffer) {
  struct deserializedTuple dTuple;
  struct iphdr ip;

  // The 'd' before variable names indicates deserialized
  uint8_t dtos = 0;
  dtos |= packetBuffer[0];
  // printf("Deserialized Tos: %hhu\n", dtos);
  packetBuffer = packetBuffer + 1;

  uint16_t dtot_len = 0;
  dtot_len |= packetBuffer[0] << 8;
  dtot_len |= packetBuffer[1];
  // printf("Deserialized tot_len: %hu\n", dtot_len);
  packetBuffer = packetBuffer + 2;

  uint16_t did = 0;
  did |= packetBuffer[0] << 8;
  did |= packetBuffer[1];
  // printf("Deserialized id: %hu\n", did);
  packetBuffer = packetBuffer + 2;

  uint16_t dfrag_off = 0;
  dfrag_off |= packetBuffer[0] << 8;
  dfrag_off |= packetBuffer[1];
  // printf("Deserialized frag_off: %hu\n", dfrag_off);
  packetBuffer = packetBuffer + 2;

  uint8_t dttl = 0;
  dttl |= packetBuffer[0];
  // printf("Deserialized ttl: %hhu\n", dttl);
  packetBuffer = packetBuffer + 1;

  uint8_t dprotocol = 0;
  dprotocol |= packetBuffer[0];
  // printf("Deserialized protocol: %hhu\n", dprotocol);
  packetBuffer = packetBuffer + 1;

  uint16_t dcheck = 0;
  dcheck |= packetBuffer[0] << 8;
  dcheck |= packetBuffer[1];
  // printf("Deserialized checksum: %hu\n", dcheck);
  packetBuffer = packetBuffer + 2;

  uint32_t dsaddr = 0;
  dsaddr |= packetBuffer[0] << 24;
  dsaddr |= packetBuffer[1] << 16;
  dsaddr |= packetBuffer[2] << 8;
  dsaddr |= packetBuffer[3];
  // printf("Deserialized saddr: %u\n", dsaddr);
  packetBuffer = packetBuffer + 4;

  uint32_t ddaddr = 0;
  ddaddr |= packetBuffer[0] << 24;
  ddaddr |= packetBuffer[1] << 16;
  ddaddr |= packetBuffer[2] << 8;
  ddaddr |= packetBuffer[3];
  // printf("Deserialized daddr: %u\n", ddaddr);
  packetBuffer = packetBuffer + 4;

  ip.tos = dtos;                  //Type of Service
  ip.tot_len = dtot_len;          //Total Length (28 bytes for IP and UDP and some data Bytes)
  ip.id = did;                    //Identification
  ip.frag_off = dfrag_off;        //Fragmentation Offset Field
  ip.ttl = dttl;                  //Time to Live
  ip.protocol = dprotocol;        //Protocol
  ip.check = dcheck;              //Checksum
  ip.saddr = dsaddr;              //Source Address
  ip.daddr = ddaddr;              //Destination Address (vip used to get ports 

  dTuple.ipReceived = ip;
  dTuple.deserializedPacketPtr = packetBuffer;
  
  return dTuple;
}

void send_rip_packets (uint16_t command, int interfaceID, char *fromAddress, char *toAddress) {
  int i, j;
  uint16_t num_entries;
  if (command == 1) {
    // Routing request
    num_entries = numRoutes;
  }
  else if (command == 2) {
    // Routing response
    num_entries = 0;
  }
  struct {
    uint32_t cost;
    uint32_t address;
  } entries[num_entries];
  
  printf("Command: %hu\n", command);
  printf("Num Entries: %hu\n", num_entries);
  for(i = 0; i < num_entries; ++i) {
    entries[i].cost = routingTable[i].cost;
    printf("Cost %d: %u\n", i, entries[i].cost);
    inet_pton(AF_INET, routingTable[i].Destination, &entries[i].address);
    printf("Address %d: %u\n", i, entries[i].address);
  }
  // Serialize packet info into buffer
  unsigned char *packetBuffer, *tempPtr;
  packetBuffer = malloc(sizeof(struct iphdr) + 2*sizeof(uint16_t) + 2*num_entries*sizeof(uint32_t));
  tempPtr = packetBuffer;
  // Need to get copy of the payload in order for checksum to work properly
  // Getting a copy of the payload
  unsigned char* ripbuf, *ripptr;
  ripbuf = malloc(2*sizeof(uint16_t) + 2*num_entries*sizeof(uint32_t));
  ripptr = ripbuf;
  // Adding entries
  ripptr[0] = command >> 8;
  ripptr[1] = command;    
  ripptr = ripptr + 2;
 
  ripptr[0] = num_entries >> 8;
  ripptr[1] = num_entries;
  ripptr = ripptr + 2;
  for(j = 0; j < num_entries; ++j) {
    ripptr[0] = entries[j].cost >> 24;
    ripptr[1] = entries[j].cost >> 16;
    ripptr[2] = entries[j].cost >> 8;
    ripptr[3] = entries[j].cost;
    ripptr = ripptr + 4;
 
    ripptr[0] = entries[j].address >> 24;
    ripptr[1] = entries[j].address >> 16;
    ripptr[2] = entries[j].address >> 8;
    ripptr[3] = entries[j].address;
    ripptr = ripptr + 4;
  }
  
  struct iphdr ip;
  ip.tos = 0;                                 //Type of Service
  ip.tot_len = sizeof(struct iphdr) + 2*sizeof(uint16_t) + num_entries*sizeof(entries);                            //Total Length (28 bytes for IP and UDP and some data Bytes)
  ip.id = interfaceID;                  //Identification
  ip.frag_off = 0;                            //Fragmentation Offset Field
  ip.ttl = MAX_TTL;                           //Time to Live
  ip.protocol = 200;                          //Protocol
  ip.check = ip_sum(ripbuf,2);                               //Checksum
  ip.saddr = inet_addr(fromAddress);    //Source Address
  ip.daddr = inet_addr(toAddress);      //Destination Address (vip used to get ports in routing tables, so forward vip along)

  tempPtr[0] = ip.tos;
  tempPtr = tempPtr + 1;

  tempPtr[0] = ip.tot_len >> 8;
  tempPtr[1] = ip.tot_len;    
  tempPtr = tempPtr + 2;

  tempPtr[0] = ip.id >> 8;
  tempPtr[1] = ip.id;
  tempPtr = tempPtr + 2;

  tempPtr[0] = ip.frag_off >> 8;
  tempPtr[1] = ip.frag_off;    
  tempPtr = tempPtr + 2;

  tempPtr[0] = ip.ttl;
  tempPtr = tempPtr + 1;

  tempPtr[0] = ip.protocol;
  tempPtr = tempPtr + 1;

  tempPtr[0] = ip.check >> 8;
  tempPtr[1] = ip.check;    
  tempPtr = tempPtr + 2;
  
  tempPtr[0] = ip.saddr >> 24;
  tempPtr[1] = ip.saddr >> 16;
  tempPtr[2] = ip.saddr >> 8;
  tempPtr[3] = ip.saddr;
  tempPtr = tempPtr + 4;

  tempPtr[0] = ip.daddr >> 24;
  tempPtr[1] = ip.daddr >> 16;
  tempPtr[2] = ip.daddr >> 8;
  tempPtr[3] = ip.daddr;
  tempPtr = tempPtr + 4;

  tempPtr[0] = command >> 8;
  tempPtr[1] = command;    
  tempPtr = tempPtr + 2;

  tempPtr[0] = num_entries >> 8;
  tempPtr[1] = num_entries;
  tempPtr = tempPtr + 2;

  // This shit broke code before when it was commented
  // printf("num_entries: %d\n", num_entries);

  for(j = 0; j < num_entries; ++j) {
    tempPtr[0] = entries[j].cost >> 24;
    tempPtr[1] = entries[j].cost >> 16;
    tempPtr[2] = entries[j].cost >> 8;
    tempPtr[3] = entries[j].cost;
    tempPtr = tempPtr + 4;

    tempPtr[0] = entries[j].address >> 24;
    tempPtr[1] = entries[j].address >> 16;
    tempPtr[2] = entries[j].address >> 8;
    tempPtr[3] = entries[j].address;
    uint32_t address = 0;

    // address |= tempPtr[0] << 24;
    // address |= tempPtr[1] << 16;
    // address |= tempPtr[2] << 8;
    // address |= tempPtr[3];
    // printf("=================================\n");
    // printf("Received address %u\n", address);
    // printf("=================================\n");
    tempPtr = tempPtr + 4;
  }
  sendMessage(0, toAddress, packetBuffer);
  free(ripbuf);
  free(packetBuffer);
}

// This converts char * into unsigned char
// unsigned char *messageConvert(unsigned char *message, unsigned char *ptr) {
//   int i = 0;
//   for(i = 0; i < strlen((const char *)message); ++i) {
//     ptr[i] = message[i];
//   }
//   return ptr;
// }

// This serializes the iphdr
unsigned char *serializeIp(char *vip, int protocol, int size, unsigned char *ptr, unsigned char * message) {
  
  struct iphdr ip;
  struct interface *curr = root;  
  ip.tos = 0;                       //Type of Service
  ip.tot_len = htons(size);         //Total Length (28 bytes for IP and UDP and some data Bytes)
  ip.id = curr->interfaceID;        //Identification
  ip.frag_off = 0;                  //Fragmentation Offset Field
  ip.ttl = MAX_TTL;                 //Time to Live
  ip.protocol = protocol;           //Protocol
  ip.check = ip_sum(message,2);                     //Checksum
  ip.saddr = inet_addr(findSourceVip());  //Source Address
  ip.daddr = inet_addr(vip);        //Destination Address (vip used to get ports in routing tables, so forward vip along)

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

  return ptr;
}

unsigned char *serializeRIP(unsigned char *ptr) {
  int i, j;

  // Packet format
  uint16_t command; // command will be 1 for request of routing info and 2 for a response
  uint16_t num_entries; // will not exceed 64 and must be 0 for a request
  struct {
    uint32_t cost; // will not exceed 16 -> define infinity to be 16
    uint32_t address; // IPv4 address
  } entries[num_entries];
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
  return ptr;
}

// Convert unsigned char * to char
// unsigned char *deserializeMessage(unsigned char *ptr, socklen_t fromLen)  {
//   int j;
//   char message[MAX_PACKET_BUFFER_SIZE];
//   for(j = 0; j < fromLen; ++j) {
//     message[j] = ptr[j];
//   }
//   return message;
// }

// This packages data on a high level and feeds it to the sendMessage method
// This will rely on helper methods for processing the IP header, message
// content, and RIP information
void *packageData(int sock, char *vip, unsigned char *message, int protocol)  {
  unsigned char *packetBuffer, *ptr;
  struct iphdr ip;

  // Packet format
  uint16_t command; // command will be 1 for request of routing info and 2 for a response
  uint16_t num_entries; // will not exceed 64 and must be 0 for a request
  struct {
    uint32_t cost; // will not exceed 16 -> define infinity to be 16
    uint32_t address; // IPv4 address
  } entries[num_entries];
  
  int size = 0;
  // Test sending messages
  if(protocol == 0) {
    sendMessage(sock, vip, message);
    return NULL;
  }
  else if(protocol == 200)  {
    size = sizeof(struct iphdr) + 2*sizeof(uint16_t) + num_entries*sizeof(entries);
  }
  else  {
    size = sizeof(struct iphdr) + sizeof(message);
  }
  packetBuffer = malloc(size);
  ptr = packetBuffer;

  // Put iphdr into packet buffer
  ptr = serializeIp(vip, protocol, size, ptr, message);

  // Put RIP info into packet buffer (if needed)
  if (protocol == 200)  {
    ptr = serializeRIP(ptr);
  }
  //If the package is not meant to be RIP, copy message to pointer
  if (protocol != 200)  {
    int i = 0;
    for (i = 0; i < strlen((const char *)message); ++i) {
      ptr[i] = message[i];
    }
  }
  // Can debug here by deserializing later

  sendMessage(sock, vip, packetBuffer);

  free(packetBuffer);
  return NULL;
}

// Prints out messages
// Will eventually have to read header and handle forwarding
// Not sure if I should handle RIP packets here or spawn a new thread
void* handleReceiveMessages () {
  struct interface *curr = root;
  struct sockaddr_in sin, from;
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
      struct deserializedTuple dTuple = deserializeIPPacket(buf);
      unsigned char *ptr = dTuple.deserializedPacketPtr;
      struct iphdr ipReceived = dTuple.ipReceived;
      uint16_t calculatedChecksum = ip_sum(ptr,2);
      if(ipReceived.check == calculatedChecksum) {
        if(ipReceived.protocol == 0)  {
          // char *message = deserializeMessage(ptr, fromLen);
          printf("Received Message: %s\n", ptr);
        }
        else if(ipReceived.protocol == 200) {
          int i, j;
          i = 0;

          uint16_t command = 0;
          command |= ptr[i] << 8;
          command |= ptr[i + 1];
          printf("==========================================\n");
          printf("RECEIVED: Deserialized Command: %hu\n", command);
          
          if (command == 1) {
            // This is a request from another node, send back a response
            // send_rip_packets(2);

          }
          else if (command == 2) {
            // This is a response, do the normal shit with it
            // if (command == 1 || command == 2) {
              // Format everything to a Route and then call update routes
            i = i + 2;
            uint16_t num_entries = 0;
            num_entries |= ptr[i] << 8;
            num_entries |= ptr[i + 1];
            i = i + 2;
            printf("RECEIVED: Deserialized Num Entries: %hu\n", num_entries);
            Route newRoutes[num_entries];
            for(j = 0; j < num_entries; ++j) {
              // Route tempRoute;
              uint32_t cost = 0;
              cost |= ptr[i] << 24;
              cost |= ptr[i + 1] << 16;
              cost |= ptr[i + 2] << 8;
              cost |= ptr[i + 3];
              i = i + 4;
              printf("RECEIVED Cost %d: %u\n", j, cost);
              uint32_t address = 0;
              address |= ptr[i] << 24;
              address |= ptr[i + 1] << 16;
              address |= ptr[i + 2] << 8;
              address |= ptr[i + 3];
              i = i + 4;
              printf("RECEIVED Address %d: %u\n", j, address);
                
              // Building the Routes
              char dest[INET_ADDRSTRLEN];
              inet_ntop(AF_INET, &address, dest, INET_ADDRSTRLEN);
              // tempRoute.Destination = dest;

              char nextHop[INET_ADDRSTRLEN];
              inet_ntop(AF_INET, &(ipReceived.saddr), nextHop, INET_ADDRSTRLEN);
              newRoutes[j].Destination = strdup(dest);
              newRoutes[j].NextHop = strdup(nextHop);
              newRoutes[j].cost = cost;
              newRoutes[j].TTL = MAX_TTL;
            }
            printf("==========================================\n");
            updateRoutingTable(newRoutes, num_entries);
          }
        }
        else  {
          // Need to parse through routingTable to see if the destination address
          // received matches a destination address in the routingTable with cost 0.
          // If so, then you have arrived at your destination! If not, then forward

          if (checkDestinationAddress(ipReceived.daddr) == 0)  {
            printf("need to forward: %s\n", ptr);
            
            int sock;
            if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
              perror("simplex-talk: socket");
              exit(1);
            }
            
            // unsigned *message = deserializeMessage(ptr, fromLen);
            char addr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(ipReceived.daddr), addr, INET_ADDRSTRLEN);
            packageData(sock, addr, ptr, 1);
          }
          else  {
            // unsigned char *message = deserializeMessage(ptr, fromLen);
            printf("Received: %s\n", ptr);
          }
        }
      }
      
    }
    sleep(5);
  }
  close(s);
  return NULL;
}

// Command line interface for users, supports commands
void handleUserInput () {
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
      unsigned char *tempMessage = (unsigned char *)strdup(splitMsg);

      packageData(sock, vip, tempMessage, 1);
    }
    else {
      printf("Invalid Command\n");
    }
    printf("\n");
  }
  // return 1;
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
    free(fromRoute);
    free(toRoute);
  }
  return 1;
}

/* Supported Commands */

// Not sure if this should be expanded later to support RIP packets and forwarding
// It probably should, in which case, we might need to change function signature
int sendMessage (int s, char * vip, unsigned char * message) {
  /*
  int sock;
  if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("simplex-talk: socket");
    exit(1);
  }
  */
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
  if (sendto(sock2, message, MAX_TRANSFER_UNIT, 0, (struct sockaddr*)&sout, soutLen) < 0) {
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

// Remove printing of own from address, currently leave it in for testing
int routes () {
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

//Find source vip
char *findSourceVip() {
  int i;
  for (i = 0; i < numRoutes; i++) {
    if(routingTable[i].cost == 0) {
      return routingTable[i].Destination;
    }
  }
  return 0;
}

// Find if the received destination address matches an address with cost 0 in the routingTable
int checkDestinationAddress(uint32_t daddr) {
  int i;
  for(i = 0; i < numRoutes; i++)  {
    if(daddr == inet_addr(routingTable[i].Destination) && routingTable[i].cost == 0) {
      return 1;
    }
  }
  return 0;
}

// Find associated port given vip
uint16_t findPort (char *vip) {
  struct interface *curr = root;
  while (curr) {
    if (strcmp(curr->toAddress, vip) == 0) {
      return curr->port;
    }
    curr = curr->next;
  }
  return root->port;
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
  return root->interfaceID;
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



