#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/select.h>
#include "maze.h"

#define MAX_CLIENTS 100
#define MAX_GROUPS 10
#define MAX_CLIENTS_PER_GROUP 3
#define STR_SIZE 2048
#define MAZE_SIZE 10000
#define TOTAL_MAZES 10
#define MAZE_SIZE_FACTOR 4
#define MONSTER_FEATURES_FACTOR 0.3
#define WAIT_TO_ATTACK_RANGE 7
#define WAIT_TO_ATTACK_RANGE_MULTIPLAYER 13

static int cli_count = 0;
static int uid = 10;
static const char USERNAME_ERROR[] = "Username already exists.\n";
static const char REGISTER_SUCCESS[] = "Registered successfully.\n";
static const char LOGIN_ERROR[] = "Log in failed.\n";
static const char LOGIN_SUCCESS[] = "Logged in successfully.\n";
static const char REGISTER[] = "R";
static const char LOGIN[] = "L";
static const char SOLO[] = "s";
static const char MULTIPLAYER[] = "m";
static const char CREATE_SOLO[] = "No solo character found. Create one\n";
static const char CREATE_MULTIPLAYER[] = "No multiplayer character found. Create one\n";
static const char MAZE_COMPLETE[] = "MAZE COMPLETE";
static const char MONSTER_DEAD[] = "MONSTER DEAD";
static const char YOU_DEAD[] = "YOU DEAD";
static const char WAIT_OTHERS[] = "Not all players are here yet. Wait...\n";
static const char ALL_HERE[] = "All players are here!\n";

typedef struct {
    int health;
    int armor;
    int attack;
    int accuracy;
    int level;
    char name[STR_SIZE];    // the name of the user that has this character
    int mode;               // solo=0, multiplayer=1
} character;

/* Client structure */
typedef struct {
    struct sockaddr_in address;
    int sockfd;
    int uid;
    character *solo;
    character *multiplayer;
    int mode;
    int groupNo;    // the number of the group to which the user was added
    // the position of the client to the game group, necessary to place user in the correct corner of the maze
    int positionInGroup;
    char name[STR_SIZE];
    char pswd[STR_SIZE];
} client_t;

/* Group structure */
typedef struct {
    int level;
    int users_count;
    client_t *users[MAX_CLIENTS_PER_GROUP];
    // lock condition for waiting all users of group
    pthread_mutex_t *lock;
    pthread_cond_t *all_here;
    // thread to start when all users of group are here
    pthread_t *group_handler;
} group_t;

// structure to hold clients coming in
client_t *clients[MAX_CLIENTS];
// structure to hold clients read from file users.dat
// newly registered incoming clients are also copied here
client_t *registered_users[MAX_CLIENTS];
int users_count = 0;
// structure to hold characters from file characters.dat
character *registered_characters[MAX_CLIENTS];
int characters_count = 0;
// structure to hold groups being created (for multiplayer game)
group_t *groups[MAX_GROUPS];
int group_count = 0;

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t characters_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t groups_mutex = PTHREAD_MUTEX_INITIALIZER;

int move_player(char playersMove, char *arr, int iPosition, int jPosition, int gridSize, int *iPlayerPosition, int *jPlayerPosition);
int move_check(char playersMove, char *arr, int *iPosition, int *jPosition, int gridSize);
int wall_check(char playersMove, char *arr, int *iPosition, int *jPosition, int gridSize);

/* trim enter character*/
void str_trim_lf(char *arr, int length) {
    int i;
    for (i = 0; i < length; i++) {
        if (arr[i] == '\n') {
            arr[i] = '\0';
            break;
        }
    }
}

/* Add clients to queue */
void queue_add(client_t *cl) {
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i]) {
            clients[i] = cl;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Remove clients from queue */
void queue_remove(int uid) {
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i]) {
            if (clients[i]->uid == uid) {
                clients[i] = NULL;
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Add groups to queue */
void queue_add_group(group_t *gr) {
    for (int i = 0; i < MAX_GROUPS; ++i) {
        if (!groups[i]) {
            groups[i] = gr;
            break;
        }
    }
}

int add_to_group(client_t *cli) {

    // if there are no groups
    if (group_count == 0) {
        group_t *gr = (group_t *) malloc(sizeof(group_t));
        gr->users_count = 0;
        // initialize mutex pointer and lock condition pointer for each new group
        gr->lock = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(gr->lock, NULL);
        gr->all_here = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
        pthread_cond_init(gr->all_here, NULL);
        // initialize thread pointer for each new group
        gr->group_handler = (pthread_t *) malloc(sizeof(pthread_t));
        queue_add_group(gr);
        group_count++;
    }

    // find next available space for incoming client
    for (int i = 0; i < group_count; i++) {
        // check if there is an available group and if max users for this group is not reached
        if (groups[i] && groups[i]->users_count < 3) {
            // add client to this group
            int position = groups[i]->users_count;
            groups[i]->users[position] = (client_t *) malloc(sizeof(client_t));
            // setting the client's the group number that they were added to
            cli->groupNo = i;

            // setting the client's position in the group array
            cli->positionInGroup = position;
            memcpy(groups[i]->users[position], cli, sizeof *cli);
            groups[i]->users_count++;
            return groups[i]->users_count;
        }
        // else create a new group
        else {
            group_t *gr = (group_t *) malloc(sizeof(group_t));
            gr->users_count = 0;
            // initialize mutex pointer and lock condition pointer for each new group
            gr->lock = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
            pthread_mutex_init(gr->lock, NULL);
            gr->all_here = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
            pthread_cond_init(gr->all_here, NULL);
            // initialize thread pointer for each new group
            gr->group_handler = (pthread_t *) malloc(sizeof(pthread_t));
            queue_add_group(gr);
            group_count++;
        }
    }
    return -1;
}

/* Send message to all clients except sender */
void send_message(char *s, int uid) {
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i]) {
            if (clients[i]->uid != uid) {
                if (write(clients[i]->sockfd, s, strlen(s)) < 0) {
                    perror("ERROR: write to descriptor failed");
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void send_level(client_t *cli) {
    char str[STR_SIZE];

    // if user is playing solo
    if (cli->mode == 0) {
        sprintf(str, "%d", cli->solo->level);
        send(cli->sockfd, str, STR_SIZE, 0);
    } else {
        sprintf(str, "%d", cli->multiplayer->level);
        send(cli->sockfd, str, STR_SIZE, 0);
    }
}

void print_character_info(client_t *cli, int solo) {
    char info[STR_SIZE];
    char str[32];
    strcpy(info, "----------------------------\n");
    strcat(info, "| YOUR CHARACTER IS READY! |\n");
    strcat(info, "----------------------------\n");
    strcat(info, "| HEALTH:               ");
    if (solo == 1) sprintf(str, "%d |\n", cli->solo->health);
    else sprintf(str, "%d |\n", cli->multiplayer->health);
    strcat(info, str);
    strcat(info, "| ARMOR:                ");
    if (solo == 1) sprintf(str, "%d |\n", cli->solo->armor);
    else sprintf(str, "%d |\n", cli->multiplayer->armor);
    strcat(info, str);
    strcat(info, "| ATTACK:               ");
    if (solo == 1) sprintf(str, "%d |\n", cli->solo->attack);
    else sprintf(str, "%d |\n", cli->multiplayer->attack);
    strcat(info, str);
    strcat(info, "| ACCURACY:             ");
    if (solo == 1) sprintf(str, "%d |\n", cli->solo->accuracy);
    else sprintf(str, "%d |\n", cli->multiplayer->accuracy);
    strcat(info, str);
    strcat(info, "----------------------------\n");
    send(cli->sockfd, info, STR_SIZE, 0);

    // send client also the level
    send_level(cli);
}

char *prepare_maze(int level, int grid, char *maze) {
    char maze_buffer[MAZE_SIZE];
    char temp_buffer[STR_SIZE];
    char *newM = maze_buffer;

    strcpy(maze_buffer, "---------------------------\n");
    sprintf(temp_buffer, "|         LEVEL %d        |\n", level);
    strcat(maze_buffer, temp_buffer);
    strcat(maze_buffer, "---------------------------\n");

    // Prints Maze
    for (int i = 0; i < grid; ++i) {
        for (int j = 0; j < grid; ++j) {
            sprintf(temp_buffer, "%c%c", *(maze + (i * grid) + j), *(maze + (i * grid) + j));
            strcat(maze_buffer, temp_buffer);
        }
        strcat(maze_buffer, "\n");
    }
    strcat(maze_buffer, "\nEnter w,s,d,a to move:");

    return newM;
}

int send_maze(client_t *cli, group_t *group) {
    // make a maze for client's level
    int level;
    // client is playing solo
    if (group == NULL) level = cli->solo->level;
    // client is playing multiplayer
    else level = group->users[0]->multiplayer->level;
    // else level = 1;
    initializeM(level * MAZE_SIZE_FACTOR);
    srand(time(NULL));
    carvePath(1, 1);
    char *maze = get_maze();

    // declaring arrays to keep players' position
    int iPlayerPosition[3];
    int jPlayerPosition[3];
    int *iPlayerPositionP[3];
    int *jPlayerPositionP[3];

    // size for maze grid
    int grid = 2 * level * MAZE_SIZE_FACTOR + 1;
    // solo
    if (group == NULL) {
        // initializing players position
        iPlayerPosition[0] = 0;
        jPlayerPosition[0] = 1;
        iPlayerPositionP[0] = &iPlayerPosition[0];
        jPlayerPositionP[0] = &jPlayerPosition[0];
        // starting 0 "the player" at the top left of the board
        *(maze + 0 * grid + 1) = '0';
    }
    // multiplayer
    else if (cli == NULL) {
        // place all players in the maze
        *(maze + 0 * grid + 1) = '1';
        *(maze + 0 * grid + grid - 2) = '2';
        *(maze + (grid - 1) * grid + 1) = '3';
        // place all players at the correct positions (for moving) regarding their position in the group array

        // place first player at 0,1
        iPlayerPosition[0] = 0;
        jPlayerPosition[0] = 1;
        iPlayerPositionP[0] = &iPlayerPosition[0];
        jPlayerPositionP[0] = &jPlayerPosition[0];

        // place second player at 0,grid-2
        iPlayerPosition[1] = 0;
        jPlayerPosition[1] = grid - 2;
        iPlayerPositionP[1] = &iPlayerPosition[1];
        jPlayerPositionP[1] = &jPlayerPosition[1];

        // place third player at grid-1,1
        iPlayerPosition[2] = grid - 1;
        jPlayerPosition[2] = 1;
        iPlayerPositionP[2] = &iPlayerPosition[2];
        jPlayerPositionP[2] = &jPlayerPosition[2];
    }
    // placing treasure at bottom right of the board
    *(maze + (grid - 1) * grid + (grid - 2)) = 'T';

    // get maze formatted to send to client
    char *newM = prepare_maze(level, grid, maze);

    // Sending formatted maze to client
    // solo
    if (group == NULL) {
        send(cli->sockfd, newM, MAZE_SIZE, 0);
    }
    // multiplayer
    else {
        for (int i = 0; i < MAX_CLIENTS_PER_GROUP; i++) {
            send(group->users[i]->sockfd, newM, MAZE_SIZE, 0);
        }
    }
    // server side logging, show maze that was sent
    printf("%s\n", newM);

    // result of moving into the maze
    int TRUE;

    // array to receive the players move from client
    char playersMove[STR_SIZE];
    // multiplayer
    if (cli == NULL) {
        int users_out = 0;

        do {    //set of socket descriptors
            fd_set readfds;

            //clear the socket set
            FD_ZERO(&readfds);
            int max_sd = 0;
            int sd, activity;
            for (int i = 0; i < MAX_CLIENTS_PER_GROUP; i++) {
                //socket descriptor
                sd = group->users[i]->sockfd;

                //if valid socket descriptor then add to read list
                if (sd > 0)
                    FD_SET(sd, &readfds);

                //highest file descriptor number, need it for the select function
                if (sd > max_sd)
                    max_sd = sd;
            }

            //wait for an activity on one of the sockets, timeout is NULL, so wait indefinitely
            activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

            int valread;
            // some IO operation on a socket
            int pos;
            for (int i = 0; i < MAX_CLIENTS_PER_GROUP; i++) {
                sd = group->users[i]->sockfd;

                if (FD_ISSET(sd, &readfds)) {
                    valread = read(sd, playersMove, STR_SIZE);

                    if (valread == 0) {
                        // user has left
                        return -1;
                    }

                    playersMove[valread] = '\0';
                    pos = i;
                    break;
                }
            }

            // move the player according to their input move
            TRUE = move_player(playersMove[0], maze, iPlayerPosition[pos], jPlayerPosition[pos], grid,
                               iPlayerPositionP[pos], jPlayerPositionP[pos]);
            if (TRUE != 2) {
                // prepare new formatted maze and send it to client
                newM = prepare_maze(level, grid, maze);

                for (int i = 0; i < MAX_CLIENTS_PER_GROUP; i++) {
                    send(group->users[i]->sockfd, newM, MAZE_SIZE, 0);
                }
            } else {
                // prepare new formatted maze and send it to client
                newM = prepare_maze(level, grid, maze);

                users_out++;
                char msg[STR_SIZE];
                for (int i = 0; i < MAX_CLIENTS_PER_GROUP; i++) {
                    if (i == pos) {
                        sprintf(msg, "You are out. Waiting for others\n");
                    } else {
                        sprintf(msg, "User %d is out. Waiting for the others\n", pos + 1);
                    }
                    strcat(msg, newM);
                    send(group->users[i]->sockfd, msg, MAZE_SIZE, 0);
                }
            }

        } while (users_out < MAX_CLIENTS_PER_GROUP || TRUE != 2);

    }
    // solo
    else if (group == NULL) {
        do {
            // receive move from client
            int receive = recv(cli->sockfd, playersMove, STR_SIZE, 0);
            if (receive == 0 || strcmp(playersMove, "exit") == 0) {
                // user has left
                return -1;
            }

            // move the player according to their input move
            TRUE = move_player(playersMove[0], maze, iPlayerPosition[0], jPlayerPosition[0], grid, iPlayerPositionP[0],
                               jPlayerPositionP[0]);
            if (TRUE != 2) {
                // prepare new formatted maze and send it to client
                newM = prepare_maze(level, grid, maze);
                printf("Sending now\n");

                send(cli->sockfd, newM, MAZE_SIZE, 0);
                printf("%s\n", newM);
            } else {
                // When player is out of the maze send a MAZE_COMPLETE message to client
                send(cli->sockfd, MAZE_COMPLETE, MAZE_SIZE, 0);
            }
        } while (TRUE != 2);
    }
    return 0;
}

character *create_monster(client_t *cli, group_t *gr) {
    character *monster = (character *) malloc(sizeof(character));

    // solo
    if (gr == NULL) {
        monster->health = cli->solo->health + cli->solo->health * MONSTER_FEATURES_FACTOR;
        monster->armor = cli->solo->armor + cli->solo->armor * MONSTER_FEATURES_FACTOR;
        monster->attack = cli->solo->attack + cli->solo->attack * MONSTER_FEATURES_FACTOR;
        monster->accuracy = cli->solo->accuracy + cli->solo->accuracy * MONSTER_FEATURES_FACTOR;
        monster->level = cli->solo->level;
    }
    // multiplayer
    else if (cli == NULL) {
        for (int i = 0; i < gr->users_count; i++) {
            monster->health +=
                    gr->users[i]->multiplayer->health + gr->users[i]->multiplayer->health * MONSTER_FEATURES_FACTOR;
            monster->armor +=
                    gr->users[i]->multiplayer->armor + gr->users[i]->multiplayer->armor * MONSTER_FEATURES_FACTOR;
            monster->attack +=
                    gr->users[i]->multiplayer->attack + gr->users[i]->multiplayer->attack * MONSTER_FEATURES_FACTOR;
            monster->accuracy +=
                    gr->users[i]->multiplayer->accuracy + gr->users[i]->multiplayer->accuracy * MONSTER_FEATURES_FACTOR;
        }
        monster->health /= 2;
        monster->armor /= 2;
        monster->attack /= 2;
        monster->accuracy /= 2;
        monster->level = gr->users[0]->multiplayer->level;
    }
    return monster;
}

void send_monster(client_t *cli, group_t *gr, character *monster, int monster_fights_back) {
    char buff_out[STR_SIZE];
    char formatted_buff[STR_SIZE];
    strcpy(buff_out, "--------------------< MAZE COMPLETE >--------------------\n\n");
    strcat(buff_out, "-----------------------> !FIGHT! <-----------------------\n");
    strcat(buff_out, "\n");
    // solo
    if (gr == NULL) {
        strcat(buff_out, "                                        __ ^ __          \n");
        strcat(buff_out, "    ||    //  ||     ||             ___|       |___      \n");
        strcat(buff_out, "     ||  //   ||     ||            |    +     +    |     \n");
        strcat(buff_out, "      ||//    ||     ||            |               |     \n");
        strcat(buff_out, "       ||     ||     ||    vs      |               |     \n");
        strcat(buff_out, "       ||     ||     ||            |      ( )      |     \n");
        strcat(buff_out, "       ||     ||     ||            |_______________|     \n");
        strcat(buff_out, "       ||     ||=====||            |_|    |_|    |_|     \n");
    }
        // multiplayer
    else if (cli == NULL) {
        strcat(buff_out, "                                        __ ^ __          \n");
        strcat(buff_out, "  ||  -----   -----                 ___|       |___      \n");
        strcat(buff_out, "  ||       |       |               |    +     +    |     \n");
        strcat(buff_out, "  ||       |       |               |               |     \n");
        strcat(buff_out, "  ||  -----   -----|     vs        |               |     \n");
        strcat(buff_out, "  ||  |            |               |      ( )      |     \n");
        strcat(buff_out, "  ||  |            |               |_______________|     \n");
        strcat(buff_out, "  ||  |_____  _____|               |_|    |_|    |_|     \n");
    }

    strcat(buff_out, "\n");
    strcat(buff_out, "---------------------------------------------------------\n");

    // solo
    if (gr == NULL) {
        sprintf(formatted_buff, "HEALTH = %d     ARMOR = %d | | HEALTH = %d     ARMOR = %d\n",
                cli->solo->health, cli->solo->armor, monster->health, monster->armor);
        strcat(buff_out, formatted_buff);
        sprintf(formatted_buff, "ATTACK = %d   ACCURACY = %d| | ATTACK = %d  ACCURACY = %d\n",
                cli->solo->attack, cli->solo->accuracy, monster->attack, monster->accuracy);
        strcat(buff_out, formatted_buff);
    }
    // multiplayer
    else if (cli == NULL) {
        for (int i = 0; i < gr->users_count; i++) {
            // to print only in the middle
            if (i != 1) {
                sprintf(formatted_buff, "HEALTH = %d     ARMOR = %d | |                          \n",
                        gr->users[i]->multiplayer->health, gr->users[i]->multiplayer->armor);
                strcat(buff_out, formatted_buff);
                sprintf(formatted_buff, "ATTACK = %d   ACCURACY = %d| |                           \n",
                        gr->users[i]->multiplayer->attack, gr->users[i]->multiplayer->accuracy);
                strcat(buff_out, formatted_buff);
            } else if (i == 1) {
                sprintf(formatted_buff, "HEALTH = %d     ARMOR = %d | | HEALTH = %d     ARMOR = %d\n",
                        gr->users[i]->multiplayer->health, gr->users[i]->multiplayer->armor, monster->health,
                        monster->armor);
                strcat(buff_out, formatted_buff);
                sprintf(formatted_buff, "ATTACK = %d   ACCURACY = %d| | ATTACK = %d  ACCURACY = %d\n",
                        gr->users[i]->multiplayer->attack, gr->users[i]->multiplayer->accuracy, monster->attack,
                        monster->accuracy);
                strcat(buff_out, formatted_buff);
            }
            strcat(buff_out, "---------------------------\n");
        }
    }

    strcat(buff_out, "---------------------------------------------------------\n");
    strcat(buff_out, "----------------------    SHOOT    ----------------------\n");
    strcat(buff_out, "-------------------- (enter any key) --------------------\n");
    if (monster_fights_back == 1) {
        strcat(buff_out, "* * * * *      <-----------------  MONSTER FIGHTS BACK! \n");
    } else if (monster_fights_back == 0) {
        strcat(buff_out, " GOT IT!     ----------------->              * * * * *\n");
    }

    if (gr == NULL) {
        send(cli->sockfd, buff_out, MAZE_SIZE, 0);
    } else if (cli == NULL) {
        for (int i = 0; i < gr->users_count; i++) {
            send(gr->users[i]->sockfd, buff_out, MAZE_SIZE, 0);
        }
    }
}

void write_user(client_t *u) {
    FILE *outfile;
    outfile = fopen("users.dat", "a");

    if (outfile == NULL) {
        fprintf(stderr, "\nError opening users file\n");
        exit(1);
    }

    // write struct to file
    fwrite(u, sizeof(client_t), 1, outfile);

    if (&fwrite != 0)
        printf("User written to file successfully !\n");
    else
        printf("error writing file!\n");

    fclose(outfile);
}

void read_users() {
    FILE *infile;
    client_t input;

    // Open users.dat for reading
    infile = fopen("users.dat", "r");
    if (infile == NULL) {
        fprintf(stderr, "\nError opening users.dat file\n");
        exit(1);
    }

    // read file contents till end of file
    while (fread(&input, sizeof(client_t), 1, infile)) {
        registered_users[users_count] = (client_t *) malloc(sizeof(client_t));
        memcpy(registered_users[users_count], &input, sizeof(client_t));
        users_count++;
    }

    // if file is empty
    if (users_count == 0) {
        registered_users[0] = (client_t *) malloc(sizeof(client_t));
    }

    fclose(infile);
}

void write_character(character *c) {
    FILE *outfile;

    // open file for writing
    outfile = fopen("characters.dat", "a");
    if (outfile == NULL) {
        fprintf(stderr, "\nError opening users file\n");
        exit(1);
    }

    // write struct to file
    fwrite(c, sizeof(character), 1, outfile);

    if (&fwrite != 0) {
        printf("Character written to file successfully!\n");
    } else
        printf("error writing character file!\n");

    fclose(outfile);
}

void read_characters() {
    FILE *charsFile;
    character input;

    // Open characters.dat for reading
    charsFile = fopen("characters.dat", "r");
    if (charsFile == NULL) {
        fprintf(stderr, "\nError opening file\n");
        exit(1);
    }

    // read file contents till end of file
    while (fread(&input, sizeof(character), 1, charsFile)) {
        registered_characters[characters_count] = (character *) malloc(sizeof(character));
        memcpy(registered_characters[characters_count], &input, sizeof(character));
        characters_count++;
    }

    if (characters_count == 0) {
        registered_characters[0] = (character *) malloc(sizeof(character));
    }

    fclose(charsFile);
}

// This function removes all previous entries in characters.dat and rewrites the characters
void update_characters() {
    FILE *outfile;
    outfile = fopen("characters.dat", "w");

    if (outfile == NULL) {
        fprintf(stderr, "\nError opening users file\n");
        exit(1);
    }

    for (int i = 0; i < characters_count; i++) {
        // write struct to file
        fwrite(registered_characters[i], sizeof(character), 1, outfile);

        if (&fwrite != 0) {
            printf("Character written to file successfully !\n");
        } else
            printf("error writing character file!\n");
    }

    if (characters_count == 0) {
        fwrite(registered_characters[0], sizeof(character), 1, outfile);

        if (&fwrite != 0) {
            printf("Character written to file successfully !\n");
        } else
            printf("error writing character file !\n");
    }

    fclose(outfile);
}

int load_user_mode(client_t *cli) {
    char buffer[STR_SIZE];
    // server side logging
    printf("Loading user mode...\n");
    // receive mode (s or m) from client

    if (recv(cli->sockfd, buffer, STR_SIZE, 0) <= 0 || strlen(buffer) >= STR_SIZE - 1) {
        printf("Wrong input.\n");
        return -1;
    } else if (strcmp(buffer, SOLO) == 0) {

        // check if user has a solo mode character. If not, prompt them to create one
        if (cli->solo == NULL) {

            // server side logging
            printf("No solo character found. Create one\n");

            // send client message to create solo character
            send(cli->sockfd, CREATE_SOLO, STR_SIZE, 0);

            bzero(buffer, STR_SIZE);
            recv(cli->sockfd, buffer, STR_SIZE, 0);

            cli->solo = malloc(sizeof *cli->solo);
            character *new_character = malloc(sizeof(character));
            strcpy(new_character->name, cli->name);
            new_character->mode = 0;
            new_character->level = 1;
            char *p = strtok(buffer, ",");

            int position = 0;
            while (p != NULL) {
                if (p != NULL && strcmp(p, "\0") != 0 && strcmp(p, "\n") != 0) {
                    str_trim_lf(p, strlen(p));
                    if (position == 0) {
                        new_character->health = atoi(p);
                    } else if (position == 1) {
                        new_character->armor = atoi(p);
                    } else if (position == 2) {
                        new_character->attack = atoi(p);
                    } else if (position == 3) {
                        new_character->accuracy = atoi(p);
                    }
                    position++;
                }
                p = strtok(NULL, ",");
            }

            // write character to file
            write_character(new_character);
            // copy character to cli
            memcpy(cli->solo, new_character, sizeof(character));

            // copy character to registered_users (because we don't reread the file)
            pthread_mutex_lock(&users_mutex);
            for (int i = 0; i < users_count; i++) {
                if (strcmp(registered_users[i]->name, cli->name) == 0) {
                    registered_users[i]->solo = (character *) malloc(sizeof(character));
                    memcpy(registered_users[i]->solo, cli->solo, sizeof(character));
                }
            }
            pthread_mutex_unlock(&users_mutex);

            pthread_mutex_lock(&characters_mutex);
            registered_characters[characters_count] = (character *) malloc(sizeof(character));
            memcpy(registered_characters[characters_count], cli->solo, sizeof(character));
            pthread_mutex_unlock(&characters_mutex);

        } else {
            // send simple signal that no character will be created
            send(cli->sockfd, "Solo found", STR_SIZE, 0);
        }
        // client is playing solo
        cli->mode = 0;
        // solo == true
        print_character_info(cli, 1);
    } else if (strcmp(buffer, MULTIPLAYER) == 0) {
        bzero(buffer, STR_SIZE);

        // check if user has a multiplayer mode character. If not, prompt them to create one
        if (cli->multiplayer == NULL) {

            // server side logging
            printf("No multiplayer character found. Create one\n");

            // send client message to create multiplayer character
            send(cli->sockfd, CREATE_MULTIPLAYER, STR_SIZE, 0);

            bzero(buffer, STR_SIZE);
            recv(cli->sockfd, buffer, STR_SIZE, 0);
            cli->multiplayer = malloc(sizeof *cli->multiplayer);

            character *new_character = malloc(sizeof(character));
            strcpy(new_character->name, cli->name);
            // character->mode == 1, is a multiplayer character
            new_character->mode = 1;
            new_character->level = 1;
            char *p = strtok(buffer, ",");

            int position = 0;
            while (p != NULL) {
                if (p != NULL && strcmp(p, "\0") != 0 && strcmp(p, "\n") != 0) {
                    str_trim_lf(p, strlen(p));
                    if (position == 0) {
                        new_character->health = atoi(p);
                    } else if (position == 1) {
                        new_character->armor = atoi(p);
                    } else if (position == 2) {
                        new_character->attack = atoi(p);
                    } else if (position == 3) {
                        new_character->accuracy = atoi(p);
                    }
                    position++;
                }
                p = strtok(NULL, ",");
            }

            // write character to file
            write_character(new_character);
            // copy character to cli
            memcpy(cli->multiplayer, new_character, sizeof(character));

            // copy character to registered_users (because we don't reread the file)
            pthread_mutex_lock(&users_mutex);
            for (int i = 0; i < users_count; i++) {
                if (strcmp(registered_users[i]->name, cli->name) == 0) {
                    registered_users[i]->multiplayer = (character *) malloc(sizeof(character));
                    memcpy(registered_users[i]->multiplayer, cli->multiplayer, sizeof(character));
                }
            }
            pthread_mutex_unlock(&users_mutex);

            pthread_mutex_lock(&characters_mutex);
            registered_characters[characters_count] = (character *) malloc(sizeof(character));
            memcpy(registered_characters[characters_count], cli->multiplayer, sizeof(character));
            pthread_mutex_unlock(&characters_mutex);
        } else {
            // send simple singal that no character will be created
            send(cli->sockfd, "Multiplayer found", STR_SIZE, 0);
        }
        // client is playing multiplayer
        cli->mode = 1;
        // solo == false;
        print_character_info(cli, 0);
    }
    return 0;
}

void handle_group(void *arg) {
    group_t *group = (group_t *) arg;

    int level = group->users[0]->multiplayer->level;
    do {
        int result = send_maze(NULL, group);
        if (result == -1) break;

        // fight monster
        character *monster = create_monster(NULL, group);
        send_monster(NULL, group, monster, -1);

        // produce random number of times that monster receives attack before attacking back
        int wait_to_attack;
        srand(time(NULL));
        wait_to_attack = rand() % WAIT_TO_ATTACK_RANGE_MULTIPLAYER;

        char attack[STR_SIZE];

        // save characters' initial health to restore it if character dies
        int initial_health[3];
        for (int i = 0; i < group->users_count; i++) {
            initial_health[i] = group->users[i]->multiplayer->health;
        }

        int fights_back = 0;
        // flag to mark if a user died
        int somebd_dead = 0;
        while (monster->health > 0 && somebd_dead != 1) {
            //set of socket descriptors
            fd_set readfds;

            //clear the socket set
            FD_ZERO(&readfds);
            int max_sd = 0;
            int sd, activity;
            for (int i = 0; i < MAX_CLIENTS_PER_GROUP; i++) {
                // server side logging
                printf("Adding child sockets to set\n");

                //socket descriptor
                sd = group->users[i]->sockfd;

                //if valid socket descriptor then add to read list
                if (sd > 0) FD_SET(sd, &readfds);

                //highest file descriptor number, need it for the select function
                if (sd > max_sd) max_sd = sd;
            }

            //wait for an activity on one of the sockets , timeout is NULL, so wait indefinitely
            activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

            int valread;
            // some IO operation on a socket
            int pos;
            for (int i = 0; i < MAX_CLIENTS_PER_GROUP; i++) {
                sd = group->users[i]->sockfd;

                if (FD_ISSET(sd, &readfds)) {
                    valread = read(sd, attack, STR_SIZE);

                    if (valread == 0) {
                        goto END;
                    }

                    attack[valread] = '\0';
                    pos = i;
                    break;
                }
            }

            // user attacks
            if (wait_to_attack > 0) {
                float damage = group->users[pos]->multiplayer->attack *
                               ((double) group->users[pos]->multiplayer->accuracy / 100.0);
                monster->health -= damage + (monster->armor / 100);
                wait_to_attack--;
                fights_back = 0;
            }
            // monster attacks
            else {
                // reseed random number
                wait_to_attack = rand() % WAIT_TO_ATTACK_RANGE_MULTIPLAYER;
                // monster fights back
                float damage = monster->attack * ((double) monster->accuracy / 100.0);
                group->users[pos]->multiplayer->health -= damage + (group->users[pos]->multiplayer->armor / 100);
                fights_back = 1;
            }

            send_monster(NULL, group, monster, fights_back);

            for (int i = 0; i < group->users_count; i++) {
                if (group->users[i]->multiplayer->health <= 0) {
                    somebd_dead = 1;
                    printf("Dead\n");
                }
            }
        }

        // check who's dead
        if (monster->health <= 0) {
            for (int i = 0; i < group->users_count; i++) {
                send(group->users[i]->sockfd, MONSTER_DEAD, MAZE_SIZE, 0);
                // increase level only if monster died
                group->users[i]->multiplayer->level++;
                // increase character's other features
                group->users[i]->multiplayer->health = initial_health[i] + group->users[i]->multiplayer->level;
                group->users[i]->multiplayer->armor += group->users[i]->multiplayer->level;
                group->users[i]->multiplayer->attack += group->users[i]->multiplayer->level;
                group->users[i]->multiplayer->accuracy += group->users[i]->multiplayer->level;
            }

            // copy new values to registered_characters and registered_users
            for (int i = 0; i < characters_count; i++) {
                for (int j = 0; j < group->users_count; j++) {
                    if (strcmp(registered_characters[i]->name, group->users[j]->name) == 0 &&
                        registered_characters[i]->mode == 1) {
                        pthread_mutex_lock(&characters_mutex);

                        registered_characters[i] = (character *) malloc(sizeof(character));
                        memcpy(registered_characters[i], group->users[j]->multiplayer, sizeof(character));
                        pthread_mutex_unlock(&characters_mutex);
                    }
                }
            }

            for (int i = 0; i < users_count; i++) {
                for (int j = 0; j < group->users_count; j++) {
                    if (strcmp(registered_users[i]->name, group->users[j]->name)) {
                        pthread_mutex_lock(&characters_mutex);
                        registered_users[i]->multiplayer = (character *) malloc(sizeof(character));
                        memcpy(registered_users[i]->multiplayer, group->users[j]->multiplayer, sizeof(character));
                        pthread_mutex_unlock(&characters_mutex);
                    }
                }
            }
        } else {
            for (int i = 0; i < group->users_count; i++) {
                group->users[i]->multiplayer->health = initial_health[i];
                send(group->users[i]->sockfd, YOU_DEAD, MAZE_SIZE, 0);
            }
        }

    } while (group->users[0]->multiplayer->level < TOTAL_MAZES);

    END:
    update_characters();
    // server side logging
    printf("Handle group thread done\n");
}

// Handle all communication with the client
void *handle_client(void *arg) {
    char buff_out[STR_SIZE];
    char buffer[STR_SIZE];
    char name[STR_SIZE];
    char pswd[STR_SIZE];
    char action[STR_SIZE];
    char mode[STR_SIZE];
    int leave_flag = 0;

    cli_count++;
    client_t *cli = (client_t *) arg;

    // Check if Register or Login
    if (recv(cli->sockfd, action, STR_SIZE, 0) <= 0 || strlen(action) >= STR_SIZE - 1) {
        printf("Wrong action input.\n");
        leave_flag = 1;
    }
    // if user is registering
    else if (strcmp(action, REGISTER) == 0) {

        // receive name
        if (recv(cli->sockfd, name, STR_SIZE, 0) <= 0 || strlen(name) < 2 || strlen(name) >= STR_SIZE - 1) {
            printf("Didn't enter the name.\n");
            leave_flag = 1;
        }
        else {
            // username already exists, disconnect client
            for (int i = 0; i < users_count; i++) {
                if (strcmp(registered_users[i]->name, name) == 0) {
                    printf("Username already exists. Disconnecting...\n");
                    send(cli->sockfd, USERNAME_ERROR, STR_SIZE, 0);
                    goto EXIT;
                }
            }

            // if name is ok
            if (leave_flag != 1) {
                // copy the name to cli
                strcpy(cli->name, name);

                // server side logging
                printf(buff_out, "%s registering now\n", cli->name);
                printf("%s", buff_out);

                // receive password
                if (recv(cli->sockfd, pswd, STR_SIZE, 0) <= 0 || strlen(pswd) < 2 || strlen(pswd) >= STR_SIZE - 1) {
                    printf("Didn't enter the password.\n");
                    goto EXIT;
                }
                // copy the password to cli
                strcpy(cli->pswd, pswd);

                // write user to file
                write_user(cli);

                // copying the newly registered client to the array, because we don't reread the file after writing the user
                // so, if a user registers and tries to log in after that, he/she is not found
                pthread_mutex_lock(&users_mutex);
                registered_users[users_count] = (client_t *) malloc(sizeof(client_t));
                memcpy(registered_users[users_count], cli, sizeof(client_t));
                users_count++;
                pthread_mutex_unlock(&users_mutex);

                bzero(buffer, STR_SIZE);

                // send the client a register success message
                sprintf(buffer + strlen(buffer), "%s", REGISTER_SUCCESS);

                send(cli->sockfd, buffer, STR_SIZE, 0);
            }
            bzero(buffer, STR_SIZE);
        }

    } else if (strcmp(action, LOGIN) == 0) {
        // Login
        printf("Logging in...\n");

        // receive name
        if (recv(cli->sockfd, name, STR_SIZE, 0) <= 0 || strlen(name) < 2 || strlen(name) >= STR_SIZE - 1) {
            printf("Didn't enter the name.\n");
            leave_flag = 1;
        }
        // copy the name to cli
        strcpy(cli->name, name);

        // receive password
        if (recv(cli->sockfd, pswd, STR_SIZE, 0) <= 0 || strlen(pswd) < 2 || strlen(pswd) >= STR_SIZE - 1) {
            printf("Didn't enter the password.\n");
            leave_flag = 1;
        }
        // copy the character to cli
        strcpy(cli->pswd, pswd);

        // Loading user data
        if (leave_flag != 1) {
            char buff[STR_SIZE];

            // searching for user with the given name and password
            int found = 0;
            for (int i = 0; i < users_count; i++) {
                if (strcmp(registered_users[i]->name, cli->name) == 0
                    && strcmp(registered_users[i]->pswd, cli->pswd) == 0) {

                    found = 1;
                    // if user has a solo character (found in file and loaded at server start)
                    if (registered_users[i]->solo != NULL) {
                        // copy that character to the cli
                        cli->solo = (character *) malloc(sizeof(character));
                        memcpy(cli->solo, registered_users[i]->solo, sizeof(character));
                    }
                    // if user has a multiplayer character (found in file and loaded at server start)
                    if (registered_users[i]->multiplayer != NULL) {
                        cli->multiplayer = (character *) malloc(sizeof(character));
                        // copy that character to the cli
                        memcpy(cli->multiplayer, registered_users[i]->multiplayer, sizeof(character));
                    }
                    break;
                }
            }

            if (found == 1) {
                sprintf(buff_out, "User %s logged in\n", cli->name);
                printf("%s", buff_out);
                // send client a login success message
                send(cli->sockfd, LOGIN_SUCCESS, STR_SIZE, 0);
            } else {
                printf("User not found.\n");
                // send client a login error message
                send(cli->sockfd, LOGIN_ERROR, STR_SIZE, 0);
                goto EXIT;
            }
        }
    }

    bzero(buff_out, STR_SIZE);

    int result = load_user_mode(cli);
    if (result == -1) {
        goto EXIT;
    }

    // if client is playing solo
    if (cli->mode == 0) {
        do {
            // send maze to client respective to their level
            int result = send_maze(cli, NULL);
            // if user has left
            if (result == -1) {
                // write new user data to file
                printf("leaving\n");
                update_characters();
                goto EXIT;
            }

            // fight monster
            character *monster = create_monster(cli, NULL);
            send_monster(cli, NULL, monster, -1);

            // produce random number of times that monster. Receives attack before attacking back
            int wait_to_attack;
            srand(time(NULL));
            wait_to_attack = rand() % WAIT_TO_ATTACK_RANGE;

            char attack[STR_SIZE];

            // save character's initial health to restore it if character dies
            int initial_health = cli->solo->health;

            int fights_back = 0;
            while (monster->health > 0 && cli->solo->health > 0) {
                // receive input from client
                recv(cli->sockfd, attack, STR_SIZE, 0);
                if (wait_to_attack > 0) {
                    float damage = cli->solo->attack * ((double) cli->solo->accuracy / 100.0);
                    monster->health -= damage + (monster->armor / 100);
                    wait_to_attack--;
                    fights_back = 0;
                } else {
                    // reseed random number
                    wait_to_attack = rand() % 5;
                    // monster fights back
                    float damage = monster->attack * ((double) monster->accuracy / 100.0);
                    cli->solo->health -= damage + (cli->solo->armor / 100);
                    fights_back = 1;
                }
                send_monster(cli, NULL, monster, fights_back);
            }

            // check who's dead
            if (monster->health <= 0) {
                send(cli->sockfd, MONSTER_DEAD, MAZE_SIZE, 0);
                // increase level only if monster died
                cli->solo->level++;
                // increase character's other features
                cli->solo->health = initial_health + cli->solo->level;
                cli->solo->armor += cli->solo->level;
                cli->solo->attack += cli->solo->level;
                cli->solo->accuracy += cli->solo->level;

                // copy new values to registered_characters and registered_users

                for (int i = 0; i < characters_count; i++) {
                    printf("i = %d\n", i);
                    if (strcmp(registered_characters[i]->name, cli->name) == 0 &&
                        registered_characters[i]->mode == 0) {
                        pthread_mutex_lock(&characters_mutex);
                        memcpy(registered_characters[i], cli->solo, sizeof(character));
                        pthread_mutex_unlock(&characters_mutex);
                    }
                }

                for (int i = 0; i < users_count; i++) {
                    if (strcmp(registered_users[i]->name, cli->name)) {
                        pthread_mutex_lock(&characters_mutex);
                        registered_users[i]->solo = (character *) malloc(sizeof(character));
                        memcpy(registered_users[i]->solo, cli->solo, sizeof(character));
                        pthread_mutex_unlock(&characters_mutex);
                    }
                }
            }
            else {
                cli->solo->health = initial_health;
                send(cli->sockfd, YOU_DEAD, MAZE_SIZE, 0);
            }
        } while (cli->solo->level < TOTAL_MAZES);
    }

    else if (cli->mode == 1) {  // if client is playing multiplayer

        // get number of users in the group, to which client was added
        int position = add_to_group(cli);
        // lock the lock for client's group
        pthread_mutex_lock(groups[cli->groupNo]->lock);
        if (groups[cli->groupNo]->users_count < 3) {
            printf("Users count %d\n", groups[cli->groupNo]->users_count);

            // waiting for all clients to arrive
            send(cli->sockfd, WAIT_OTHERS, STR_SIZE, 0);
            pthread_cond_wait(groups[cli->groupNo]->all_here, groups[cli->groupNo]->lock);
        } else {
            // send message all here to clients
            for (int i = 0; i < MAX_CLIENTS_PER_GROUP; i++)
                send(groups[cli->groupNo]->users[i]->sockfd, ALL_HERE, STR_SIZE, 0);
        }

        // release the lock for client's group
        pthread_mutex_unlock(groups[cli->groupNo]->lock);

        // one server thread starts the group handling thread
        if (position == 3) {
            pthread_create(groups[cli->groupNo]->group_handler, NULL, (void *) handle_group,
                           (void *) groups[cli->groupNo]);
            // the thread that created the handle_group thread waits for it to finish
            pthread_join(*groups[cli->groupNo]->group_handler, NULL);
        }
    }

    while (1) {
        if (leave_flag) break;

        int receive = recv(cli->sockfd, buff_out, STR_SIZE, 0);
        if (receive > 0) {
            if (strlen(buff_out) > 0) {
                send_message(buff_out, cli->uid);

                str_trim_lf(buff_out, strlen(buff_out));
                sprintf("%s -> %s\n", buff_out, cli->name);
            }
        } else if (receive == 0 || strcmp(buff_out, "exit") == 0) {
            sprintf(buff_out, "%s has left\n", cli->name);
            printf("%s", buff_out);
            send_message(buff_out, cli->uid);
            leave_flag = 1;
        } else {
            printf("ERROR: -1\n");
            leave_flag = 1;
        }

        bzero(buff_out, STR_SIZE);
    }

    // Delete client from queue and yield thread
    EXIT:
    printf("%s has left\n", cli->name);
    close(cli->sockfd);

    queue_remove(cli->uid);
    free(cli);
    cli_count--;
    pthread_detach(pthread_self());
    return NULL;
}

int main(int argc, char **argv) {
    char *ip = "127.0.0.1";

    int option = 1;
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    // load users from file
    read_users();
    read_characters();

    // loading data to structures and server side logging
    for (int i = 0; i < users_count; i++) {
        printf("name: %s, password: %s\n", registered_users[i]->name, registered_users[i]->pswd);

        for (int j = 0; j < characters_count; j++) {
            if (strcmp(registered_characters[j]->name, registered_users[i]->name) == 0) {
                // if solo character is found
                if (registered_characters[j]->mode == 0) {
                    registered_users[i]->solo = (character *) malloc(sizeof(character));
                    memcpy(registered_users[i]->solo, registered_characters[j], sizeof(character));

                    // server side logging
                    printf("Solo character copied\n");
                } else {
                    registered_users[i]->multiplayer = (character *) malloc(sizeof(character));
                    memcpy(registered_users[i]->multiplayer, registered_characters[j], sizeof(character));

                    // server side logging
                    printf("Multiplayer character copied\n");
                }
            }
        }
    }

    // Socket settings
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    serv_addr.sin_port = htons(8080);

    /* Ignore pipe signals */
    signal(SIGPIPE, SIG_IGN);

    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option))){
        perror("ERROR: setsockopt failed.\n");
        return EXIT_FAILURE;
    }

    // Bind
    if (bind(listenfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR: Socket binding failed.\n");
        return EXIT_FAILURE;
    }

    // Listen
    if (listen(listenfd, 10) < 0) {
        perror("ERROR: Socket listening failed.\n");
        return EXIT_FAILURE;
    }

    printf("=== WELCOME TO THE GAME ===\n");

    while (1) {
        socklen_t clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr *) &cli_addr, &clilen);

        /* Check if max clients is reached */
        if ((cli_count + 1) == MAX_CLIENTS) {
            printf("Max clients reached.\n");
            close(connfd);
            continue;
        }

        /* Client settings */
        client_t *cli = (client_t *) malloc(sizeof(client_t));
        cli->address = cli_addr;
        cli->sockfd = connfd;
        cli->uid = uid++;

        /* Add client to the queue and fork thread */
        queue_add(cli);
        pthread_create(&tid, NULL, &handle_client, (void *) cli);

        sleep(1);
    }
    return EXIT_SUCCESS;
}

int move_player(char playersMove, char *arr, int iPosition, int jPosition, int gridSize, int *iPlayerPosition,
                int *jPlayerPosition) {
    int tempi = iPosition;
    int tempj = jPosition;
    int TRUE;

    if (playersMove == 'w') {
        // moving up
        *iPlayerPosition -= 1;
    } else if (playersMove == 's') {
        // moving down
        *iPlayerPosition += 1;
    } else if (playersMove == 'd') {
        // moving right
        *jPlayerPosition += 1;
    } else if (playersMove == 'a') {
        // moving left
        *jPlayerPosition -= 1;
    }

    TRUE = move_check(playersMove, arr, iPlayerPosition, jPlayerPosition, gridSize);
    if (TRUE == 1 && wall_check(playersMove, arr, iPlayerPosition, jPlayerPosition, gridSize) == 1) {
        //swap arrays
        char temp = *(arr + tempi * gridSize + tempj);

        *(arr + tempi * gridSize + tempj) = *(arr + *iPlayerPosition * gridSize + *jPlayerPosition);

        *(arr + *iPlayerPosition * gridSize + *jPlayerPosition) = temp;

        return TRUE;
    } else if (TRUE == 2) {
        *(arr + iPosition * gridSize + jPosition) = ' ';
        return TRUE;
    }
    else {
        *iPlayerPosition = iPosition;
        *jPlayerPosition = jPosition;
        return TRUE;
    }
}

int move_check(char playersMove, char *arr, int *iPosition, int *jPosition, int gridSize) {
    if (*(arr + (*iPosition) * gridSize + (*jPosition)) == '|' ||
        *(arr + (*iPosition) * gridSize + (*jPosition)) == '1' ||
        *(arr + (*iPosition) * gridSize + (*jPosition)) == '2' ||
        *(arr + (*iPosition) * gridSize + (*jPosition)) == '3') {
        // if found wall or other player
        printf("Can't move there\n");
        return 0;
    }
    if (*(arr + (*iPosition) * gridSize + (*jPosition)) == 'T') {
        // if found TT at the end of the maze
        return 2;
    }

    return 1;
}

int wall_check(char playersMove, char *arr, int *iPosition, int *jPosition, int gridSize) {
    //Handling the corners
    if (*iPosition < 0 && *jPosition < 0 && (playersMove == 'w' || playersMove == 'a')) {
        printf("Can't move there\n");
        return 0;
    } else if (*iPosition < 0 && *jPosition > gridSize && (playersMove == 'w' || playersMove == 'd')) {
        printf("Can't move there\n");
        return 0;
    } else if (*iPosition > gridSize && *jPosition < 0 && (playersMove == 's' || playersMove == 'a')) {
        printf("Can't move there\n");
        return 0;
    } else if (*iPosition > gridSize && *jPosition < 0 && (playersMove == 's' || playersMove == 'd')) {
        printf("Can't move there\n");
        return 0;
    } else {
        // handling the ith rows and jth columns
        if (*iPosition < 0 && playersMove == 'w') {
            printf("Can't move there\n");
            return 0;
        }
        if (*iPosition > gridSize && playersMove == 's') {
            printf("Can't move there\n");
            return 0;
        }
        if (*jPosition < 0 && playersMove == 'a') {
            printf("Can't move there\n");
            return 0;
        }
        if (*jPosition > gridSize && playersMove == 'd') {
            printf("Can't move there\n");
            return 0;
        }
    }
    return 1;
}