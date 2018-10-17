#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "client.h"

bool receivedMessage = false;

void sigalarm_clean(client_t *data)
{
  static client_t *sClient;
  if (data)
  {
    sClient = data;
  }
  else
  {
    writeTimeOutMessage(sClient);
    free_client(sClient);
  }
}

void sigalarm_handler(int signo)
{
  if (!receivedMessage)
  {
    sigalarm_clean(NULL);
    exit(1);
  }
}

void sigalarm_install()
{
  struct sigaction sa;

  sa.sa_handler = sigalarm_handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);

  if (sigaction(SIGALRM, &sa, NULL) == -1)
  {
    fprintf(stderr, "Unable to install handler\n");
    exit(1);
  }
}

void badMessageAlloc()
{
  fprintf(stderr, "Unable to allocate memory for clog output\n");
  exit(7);
}

client_t *new_client()
{

  client_t *client = (client_t *)malloc(sizeof(client_t));

  client->request = (request_t *)malloc(sizeof(request_t));
  client->answer = (answer_t *)malloc(sizeof(answer_t));

  client->pid = getpid();

  return client;
}

void createRequest(client_t *client, int num_wanted_seats, int num_pref_seats, int *pref_seat_list)
{
  request_t *req = client->request;

  req->num_wanted_seats = num_wanted_seats;
  req->num_pref_seats = num_pref_seats;
  req->pid = (int)client->pid;
  memcpy(req->pref_seat_list, pref_seat_list, sizeof(int) * num_pref_seats);
  memcpy(req->answer_fifo_name, client->answer_fifo_name, MAX_ANS_FIFO);

  client->fd_log = open(CLIENT_LOG, O_WRONLY | O_APPEND | O_CREAT, S_IRWXU);
  client->fd_book = open(CLIENT_BKS, O_WRONLY | O_APPEND | O_CREAT, S_IRWXU);

  if (client->fd_log == -1 || client->fd_book == -1)
  {
    fprintf(stderr, "Unable to create or open clog/cbook file\n");
    exit(6);
  }
}

void openRequestFifo(client_t *client)
{
  int fd = open(REQ_FIFO, O_WRONLY);

  if (fd == -1)
  {
    perror("Error");
    fprintf(stderr, "Failed to open requests fifo\n");
    exit(1);
  }
  client->fdRequest = fd;
}

void createAnswerFifo(client_t *client)
{
  char *name = NULL;

  if (asprintf(&name, ANS_FIFO "%d", client->pid) == -1)
  {
    fprintf(stderr, "unable to create name\n");
    exit(3);
  }

  int i = mkfifo(name, S_IRUSR | S_IWUSR);

  if (i == -1)
  {

    if (errno == EEXIST)
    {

      unlink(name);
      mkfifo(name, S_IRUSR | S_IWUSR);
    }
    else
    {

      fprintf(stderr, "Failed to create answer fifo\n");
      exit(4);
    }
  }

  client->answer_fifo_name = name;
}

void openAnswerFifo(client_t *client)
{
  int fd_ans = open(client->answer_fifo_name, O_RDONLY);

  if (fd_ans == -1)
  {
    fprintf(stderr, "Error opening answer fifo\n");
    exit(5);
  }

  client->fdAnswer = fd_ans;
}

void sendRequest(client_t *client)
{
  //TODO: Possiveis erros: verificar valor do fd e do req
  if (write(client->fdRequest, client->request, sizeof(request_t)) == -1)
  {
    perror("Error");
    fprintf(stderr, "Error writing to fifo request\n");
    exit(2);
  }
}

void readAnswer(client_t *client)
{

  int n;

  while ((n = read(client->fdAnswer, client->answer, sizeof(answer_t))) != -1)
  {

    if (n == 0)
    {
      continue;
    }
    else
      break;
  }

  receivedMessage = true;
}

void displayAnswer(answer_t *answer)
{
  printf("--->Message\n");
  printf("response_value= %d\n", answer->response_value);
  printf("num_reserved_seats= %d \n", answer->num_reserved_seats);

  if (answer->num_reserved_seats != 0)
  {
    int i = 0;
    printf("Seats: ");
    for (; i < answer->num_reserved_seats; i++)
    {
      printf("%d ", answer->reserved_seat_list[i]);
    }
    printf("\n");
  }
}

void free_client(client_t *client)
{
  close(client->fdRequest);
  free(client->request);

  close(client->fdAnswer);
  if (unlink(client->answer_fifo_name) == -1)
  {
    perror("Error unlinking answer_fifo_name");
  }
  close(client->fd_book);
  close(client->fd_log);
  free(client->answer);
  free(client->answer_fifo_name);
}

void writeLog(client_t *client)
{
  char *pid_msg = NULL;

  char *pid_fmt = "%." QUOTE(WIDTH_PID) "d";

  int i = asprintf(&pid_msg, pid_fmt, client->pid);

  if (i == -1)
    badMessageAlloc();

  if (client->answer->response_value == VALID_RESERVATION)
    writeValidMessage(client, pid_msg);
  else
    writeInvalidMessage(client, pid_msg);

  free(pid_msg);
}

void writeInvalidMessage(client_t *client, char *pid_msg)
{
  int answer = client->answer->response_value;

  char *reason = NULL;

  switch (answer)
  {
  case INVALID_NUM_SEATS:
    reason = MAX;
    break;
  case INVALID_PREF_NUMBER:
    reason = NST;
    break;
  case INVALID_SEATS_ID:
    reason = IID;
    break;
  case INVALID_PARAMETHERS:
    reason = ERR;
    break;
  case UNAVALIABLE_SEAT:
    reason = NAV;
    break;
  case ROOM_FULL:
    reason = FUL;
    break;
  default:
    break;
  }

  if (reason != NULL)
  {
    char *final_msg = NULL;
    int i = asprintf(&final_msg, "%s %s\n", pid_msg, reason);

    if (i == -1)
      badMessageAlloc();

    write(client->fd_log, final_msg, i);
    free(final_msg);
  }
}

void writeTimeOutMessage(client_t *client)
{

  char *out = OUT;

  char *pid_msg = NULL;

  char *pid_fmt = "%." QUOTE(WIDTH_PID) "d";

  int i = asprintf(&pid_msg, pid_fmt, client->pid);

  if (i == -1)
    badMessageAlloc();

  char *final_msg = NULL;

  i = asprintf(&final_msg, "%s %s\n", pid_msg, out);

  if (i == -1)
    badMessageAlloc();

  write(client->fd_log, final_msg, i);
  free(final_msg);
}

void writeValidMessage(client_t *client, char *pid_msg)
{
  char *id = NULL;
  char *seat = NULL;
  char *final_msg = NULL;
  int j = 0;
  int i = 0;

  char *seat_fmt = "%." QUOTE(WIDTH_SEAT) "d";
  char *id_fmt = "%." QUOTE(WIDTH_XX) "d.%." QUOTE(WIDTH_NN) "d";

  for (; j < client->answer->num_reserved_seats; j++)
  {
    i = asprintf(&id, id_fmt, (j + 1), client->answer->num_reserved_seats);
    if (i == -1)
      badMessageAlloc();

    i = asprintf(&seat, seat_fmt, client->answer->reserved_seat_list[j]);
    if (i == -1)
      badMessageAlloc();

    write(client->fd_book, seat, i);
    write(client->fd_book, "\n", 1);

    i = asprintf(&final_msg, "%s %s %s\n", pid_msg, id, seat);
    if (i == -1)
      badMessageAlloc();

    write(client->fd_log, final_msg, i);

    //freeing messages so no unecessary memory is kept occupied
    free(id);
    free(seat);
    free(final_msg);
  }
}

int string_to_array(char *pref_list, int **pref_seat_list)
{

  char *pch;
  int *new_pos;
  int pref_number = 0;

  pch = strtok(pref_list, " ");

  if (pch == NULL)
  {
    fprintf(stderr, "Error on preffered seat list\n");
    exit(1);
  }

  while (pch != NULL)
  {
    pref_number++;
    new_pos = (int *)realloc(*pref_seat_list, pref_number * sizeof(int));

    if (new_pos != NULL)
    {
      *pref_seat_list = new_pos;
      (*pref_seat_list)[pref_number - 1] = atoi(pch);
    }
    else
    {
      free(new_pos);
      fprintf(stderr, "Error allocating space for pref seat list\n");
      exit(1);
    }
    pch = strtok(NULL, " ");
  }

  return pref_number;
}

int main(int argc, char *argv[])
{

  //TODO: Testar possiveis erros;
  if (argc != 4)
  {
    printf("Usage: %s <time_out> <num_wanted_seats> <pref_seat_list>\n", argv[0]);
    exit(1);
  }

  int time_out = atoi(argv[1]);
  int num_wanted_seats = atoi(argv[2]);
  int *pref_seat_list = NULL;
  int pref_number = string_to_array(argv[3], &pref_seat_list);

  if (pref_number > MAX_CLI_SEATS)
    exit(1);

  client_t *client = new_client();

  sigalarm_install();
  sigalarm_clean(client);

  createAnswerFifo(client);
  createRequest(client, num_wanted_seats, pref_number, pref_seat_list);
  openRequestFifo(client);
  sendRequest(client);

  //-----starting timer-----
  ualarm(time_out * 1000, 0);
  //------------------------

  close(client->fdRequest);
  openAnswerFifo(client);
  readAnswer(client);
  writeLog(client);
  free_client(client);

  return 0;
}