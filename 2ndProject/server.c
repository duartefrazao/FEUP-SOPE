#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "server.h"

pthread_mutex_t unit_buffer_mut = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t unit_buffer_cond = PTHREAD_COND_INITIALIZER;
int unit_buffer_full = 0;
int g_num_room_seats = 0; 
request_t request_buffer;

void * thr_run(void * thr){
  thread_t * thread = (thread_t *) thr;
  
  while(1){

    int answer;
    printf("reading request..\n");
    readRequestThread(thread);
    printf("read request\n");

    if((answer = validateRequestThread(thread)) != 0){

      printf("------------->invalid answer\n");
      sendFailedAnswer(thread, answer);

      //TODO continue or break?
      continue;
    }
    printf("processing request\n");
    processRequest(thread);
    sendAnswer(thread);
  }
}


server_t * new_server(int num_room_seats, int num_ticket_offices, int open_time){
  
  server_t * server = (server_t *) malloc(sizeof(server_t));
  
  server->num_room_seats = num_room_seats;
  server->num_ticket_offices = num_ticket_offices;
  g_num_room_seats = num_room_seats;
  server->open_time = open_time;

  server->room_seats = (Seat**) malloc(server->num_room_seats * sizeof(Seat*));
  for(unsigned int i = 0; i < num_room_seats; i++){
    server->room_seats[i] = (Seat*) malloc(sizeof(Seat));
    (server->room_seats[i])->reserved = false;

    if( pthread_mutex_init(&(server->room_seats[i]->mutex), NULL) != 0){
      fprintf(stderr, "Unable to init mutex\n"); 
      exit (5);
    }
  }

  return server;
}

void free_server(server_t *server){
  for(unsigned int i = 0; i < server->num_room_seats; i++){
    free(server->room_seats[i]);
  }
  free(server->room_seats);
}

void createRequestFifo(server_t *server){
  if( mkfifo(REQ_FIFO, 0755) == -1){
    fprintf(stderr, "Unable to open fifo req\n");
    exit(1);
  }
}

void openRequestFifo(server_t *server){
  int fd = open(REQ_FIFO, O_RDONLY);

  if(fd == -1){
    fprintf(stderr, "error opening fifo req\n");
    exit(1);
  }
  server->fdRequest = fd;
}

void readRequestServer(server_t *server){
  int n;
  
  while(1){
    printf("Locking server\n");
    pthread_mutex_lock(&unit_buffer_mut);
    fprintf(stderr, "Lock in Server\n");
    if((n = read(server->fdRequest,&request_buffer,sizeof(request_t))) != -1 && unit_buffer_full == 0){
      pthread_mutex_unlock(&unit_buffer_mut);
      fprintf(stderr, "Unlock Server\n");
      unit_buffer_full = 1;
      pthread_cond_broadcast(&unit_buffer_cond);
      fprintf(stderr, "Send Condition Server\n");

      close(server->fdRequest);
      openRequestFifo(server);
      continue;
    }

    else {
      pthread_mutex_unlock(&unit_buffer_mut);
      printf("Unlocked Server\n");
      break;
    }
  }
}

void createThreads(server_t *server){
  server->ticket_offices = (thread_t**) malloc(server->num_room_seats * sizeof(thread_t*));
  for(unsigned int i = 0; i < server->num_ticket_offices; i++){
    server->ticket_offices[i] = new_thread();
    server->ticket_offices[i]->seats = server->room_seats;
  }
}

void runThreads(server_t *server){
  for(unsigned int i = 0; i < server->num_ticket_offices; i++){
    pthread_create(&((server->ticket_offices[i])->tid), NULL, thr_run, (server->ticket_offices[i]));
  }
}

void endThreads(server_t *server){

  for(unsigned int i = 0; i < server->num_ticket_offices; i++){
    pthread_join((server->ticket_offices[i])->tid, NULL);
    free_thread(server->ticket_offices[i]);
  }
  free(server->ticket_offices);
}




thread_t * new_thread(){
  thread_t * thread = (thread_t *) malloc(sizeof(thread_t));
  thread->request = (request_t *) malloc(sizeof(request_t)); 
  thread->answer = (answer_t *) malloc(sizeof(answer_t));
  return thread;
}

void free_thread(thread_t *thread){
  free(thread->answer);
  free(thread->request);
  free(thread);
}

void openAnswerFifo(thread_t *thread){
  int fd_ans = open(thread->request->answer_fifo_name, O_WRONLY);

  if(fd_ans == -1){
    fprintf(stderr, "Unable to open answer fifo\n");
    exit(3);
  }
  thread->fdAnswer = fd_ans;
}

void readRequestThread(thread_t *thread){
 
  fprintf(stderr, "Locking Thread\n");
  pthread_mutex_lock(&unit_buffer_mut);
  printf("Locked in thread\n");

      while(unit_buffer_full != 1){
        fprintf(stderr, "Wait Condition Server\n");
        pthread_cond_wait(&unit_buffer_cond,&unit_buffer_mut);
      }
       
      
      unit_buffer_full = 0;
      
      *(thread->request) = request_buffer;
   

  pthread_mutex_unlock(&unit_buffer_mut);
  printf("Unlocked in thread\n");
}

int validateRequestThread(thread_t * thread){

  //seats quantity
  if(thread->request->num_wanted_seats > MAX_CLI_SEATS || thread->request->num_wanted_seats < 1)
    return INVALID_NUM_SEATS;
  
  int  i = 0;


  if(thread->request->num_pref_seats < thread->request->num_wanted_seats || thread->request->num_pref_seats > MAX_CLI_SEATS)
     return INVALID_PREF_NUMBER;

  //seats identifiers
  for(; i < thread->request->num_wanted_seats; i++){
    if(thread->request->pref_seat_list[i] < 1 || thread->request->pref_seat_list[i] > g_num_room_seats)
      return INVALID_SEATS_ID;
  }

  return 0;
}

int isSeatFree(Seat *seats, int seatNum){

    return seats[seatNum].reserved != true;

}
void bookSeat(Seat *seats, int seatNum, int clientId){

  seats[seatNum].reserved = true;
  seats[clientId].clientID = clientId;

  return;

}
void freeSeat(Seat *seats, int seatNum){

  seats[seatNum].reserved = false;
  seats[seatNum].clientID = 0; 

}


int processRequest(thread_t * thread){

  int i = 0; 

  int numberOfSeatsReserved = 0; 

  for(; i < thread->request->num_pref_seats; i++){

    int seatNum = thread->request->pref_seat_list[i];

    pthread_mutex_lock(&( thread->seats[seatNum]->mutex ));

    if(isSeatFree(( *thread->seats), seatNum)){
      bookSeat((*thread->seats), seatNum, 2);
      numberOfSeatsReserved++;
    }else{
      printf("Occupied\n");
    }

    if(numberOfSeatsReserved == thread->request->num_wanted_seats){
      printf("-------------->reserved all seats\n");
      pthread_mutex_unlock( & (thread->seats[seatNum]->mutex)); 
      return 0; 
    }

    pthread_mutex_unlock( & (thread->seats[seatNum]->mutex));
  }

  return 1;
}

void sendAnswer(thread_t *thread){
  if ( write(thread->fdAnswer, thread->answer, sizeof(answer_t)) == -1){
    printf("error writing answer\n");
    exit(4);
  }
}

void sendFailedAnswer(thread_t * thread, int value){

  thread->answer->response_value = value;
  thread->answer->num_reserved_seats = 0; 
  thread->answer->reserved_seat_list = NULL;

  if( write(thread->fdAnswer, thread->answer, sizeof(answer_t)) == -1){
    printf("error writing answer\n"); 
    exit(4);
  }



}

int main(int argc,  char* argv[]){

  int num_room_seats = atoi(argv[1]);
  int num_ticket_offices = atoi(argv[2]);
  int open_time = atoi(argv[3]);

  server_t * server = new_server(num_room_seats, num_ticket_offices, open_time);
  createRequestFifo(server);
  openRequestFifo(server);
  createThreads(server);
  runThreads(server);

  //TODO: Usar variaveis de condicao para evitar busy waiting
  readRequestServer(server);

  endThreads(server);
  free_server(server);

  return 0;
}