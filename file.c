  #define _GNU_SOURCE
   #include <stdio.h>
   #include <stdlib.h>

   int readFile(void) {
       FILE * fp;
       char * line = NULL;
       size_t len = 0;
       ssize_t read;

       fp = fopen("/etc/motd", "r");
       if (fp == NULL)
           exit(EXIT_FAILURE);

// makes each node with first line
       first = getline(&line, &len, fp);
       nodeInfo = strtok(first, ":")
       makeNode(nodeInfo[0], nodeInfo[1]);

//reads rest of file
       while ((read = getline(&line, &len, fp)) != -1) {
           printf("Retrieved line of length %zu :\n", read);
           printf("%s", line);

       }
// end reading file.
       fclose(fp);
       if (line)
           free(line);
       exit(EXIT_SUCCESS);
   }


void makeNode(char address, int port){
	typedef struct myNode{
		char ip_address;
		int myPort;
	}
}

//does stuff with VIP of other nodes
void nodePath{
	
}
