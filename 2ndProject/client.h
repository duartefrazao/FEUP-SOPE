#ifndef _CLIENT_H_
#define _CLIENT_H_

#include "structs.h"

typedef struct{
    pid_t pid;

    request_t * request;
    answer_t * answer;

    int fdRequest;
    int fdAnswer;
    
    char * answer_fifo_name;
} client_t;

client_t * new_client();
void free_client(client_t *client);

void createAnswerFifo(client_t *client);
void openRequestFifo(client_t *client);
void openAnswerFifo(client_t *client);

void createRequest(client_t *client, int time_out, int num_wanted_seats, int *pref_seat_list);
void sendRequest(client_t *client);
void readAnswer(client_t *client);

#endif