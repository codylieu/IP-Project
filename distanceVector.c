#include<stdio.h>

#define MAX_ROUTES 128 /* max size of routing table*/
#define MAX_TTL 120 /* seconds until route expires*/

typedef struct {
    NodeAddr Destination;
    NodeAddr NextHop;
    int cost;
    u_short TTL;
    } Route;

// make array routingTable
//make bool array of same size: true means entry is valid, false means not (TTL = 0)
int numRoutes = 0;
Route = routingTable[MAX_ROUTES];

void mergeRoute (Route *new){
    int i;
    for (i=0;i<numRoutes;i++){
        --routingTable[i].TTL ;
        if(routingTable[i].TTL == 0){
            
        }

        if(new -> Destination == routingTable[i].Destination){
            if(new -> cost +1 < routingTable[i].cost){
                break;
            }
            else if(new -> NextHop == routingTable[i].NextHop){
                break;
            }
            else {
                return;
            }       
        }
    }
    if(i == numRoutes){
        /*new route */
        if(numRoute < MAX_ROUTES){
            ++numRoutes;
        }
        else{
            return;
        }
    }

    routingTable[i] = *new;
    routingTable[i].TTL = MAX_TTL;
    ++routingTable[i].cost;

}

void updateRoutingTable(Route *newRoute, int numNewRoutes){
    int i;
    for(i=0; i<numNewRoutes;i++){
        mergeRoute(&newRoute[i]);
    }
}