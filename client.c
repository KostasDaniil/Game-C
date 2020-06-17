#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define STR_SIZE 2048
#define LENGTH 2048
static const char REGISTER_SUCCESS[] = "Registered successfully.\n";
static const char LOGIN_ERROR[] = "Log in failed.\n";
static const char LOGIN_SUCCESS[] = "Logged in successfully.\n";
static const char REGISTER[] = "R";
static const char LOGIN[] = "L";
static const char SOLO[] = "s";
static const char MULTIPLAYER[] = "m";
static const char CREATE_SOLO[] = "No solo character found. Create one\n";
static const char CREATE_MULTIPLAYER[] = "No multiplayer character found. Create one\n";
static const char WAIT_OTHERS[] = "Not all players are here yet. Wait...\n";

volatile sig_atomic_t flag = 0;
int sockfd = 0;
char name[STR_SIZE];
char pswd[STR_SIZE];
char buff[64];
char action[STR_SIZE];
char status[STR_SIZE];
char mode[STR_SIZE];

int iPlayerPosition = 0, jPlayerPosition = 1;
char playersMove;
int level;
int TRUE;

void str_trim_lf(char *arr, int length) {
    int i;
    for (i = 0; i < length; i++) { // trim \n
        if (arr[i] == '\n') {
            arr[i] = '\0';
            break;
        }
    }
}

void trim_trailing_spaces(char *str) {
    int index, i;

    index = -1;
    i = 0;
    while (str[i] != '\0') {
        if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n') {
            index = i;
        }
        i++;
    }

    str[index + 1] = '\0';
}

void catch_ctrl_c_and_exit(int sig) {
    flag = 1;
}

void send_msg_handler() {

    char message[LENGTH] = {};
    char buffer[LENGTH + STR_SIZE] = {};

    while (1) {
        fgets(message, LENGTH, stdin);

        if (strcmp(message, "exit") == 0) {
            break;
        } else {
            sprintf(buffer, "%s\n", message);
            send(sockfd, message, strlen(message), 0);
        }

        bzero(message, LENGTH);
        bzero(buffer, LENGTH + STR_SIZE);
    }
    catch_ctrl_c_and_exit(2);
}

void recv_msg_handler() {
    char message[10000] = {};
    while (1) {
        int receive = recv(sockfd, message, 10000, 0);
        if (receive > 0) {
            system("clear");
            printf("%s\n", message);
        } else if (receive == 0) {
            break;
        } else {
        }
        memset(message, 0, sizeof(message));
    }
}

int main(int argc, char **argv) {
    char *ip = "127.0.0.1";

    signal(SIGINT, catch_ctrl_c_and_exit);

    printf("Register or Login? (R/L): ");
    fgets(action, STR_SIZE, stdin);
    str_trim_lf(action, strlen(action));
    if (strcmp(action, REGISTER) != 0 && strcmp(action, LOGIN) != 0) {
        printf("Invalid input. Only R or L are accepted\n");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;

    /* Socket settings */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(8080);


    // Connect to Server
    int err = connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr));
    if (err == -1) {
        printf("ERROR: connect\n");
        return EXIT_FAILURE;
    }

    // Sending action to server
    send(sockfd, action, STR_SIZE, 0);

    if (strcmp(action, REGISTER) == 0) {

        //Register
        printf("Registering...\n");
        //Enter username
        printf("Please enter a username (max 30 characters)\n");
        fgets(name, STR_SIZE, stdin);

        str_trim_lf(name, strlen(name));
        trim_trailing_spaces(name);

        if (strlen(name) > STR_SIZE || strlen(name) < 2) {
            printf("Name must be less than 30 and more than 2 characters.\n");
            return EXIT_FAILURE;
        }

        send(sockfd, name, STR_SIZE, 0);

        // password
        printf("Please enter a password (max 30 characters)\n");
        fgets(pswd, STR_SIZE, stdin);

        str_trim_lf(pswd, strlen(pswd));
        trim_trailing_spaces(pswd);

        if (strlen(pswd) > STR_SIZE || strlen(pswd) < 2) {
            printf("Password must be less than 30 and more than 2 characters.\n");
            return EXIT_FAILURE;
        }

        send(sockfd, pswd, STR_SIZE, 0);
        recv(sockfd, status, STR_SIZE, 0);

        if (strncmp(status, REGISTER_SUCCESS, 24) == 0) {
            printf("%s\n", status);
        } else {
            printf("%s\n", status);
            return EXIT_FAILURE;
        }

    } else if (strcmp(action, LOGIN) == 0) {
        //Login
        printf("Logging in...\n");

        //Enter username
        printf("Please enter your username (max 30 characters)\n");
        fgets(name, STR_SIZE, stdin);

        str_trim_lf(name, strlen(name));
        trim_trailing_spaces(name);

        if (strlen(name) > STR_SIZE || strlen(name) < 2) {
            printf("Name must be less than 30 and more than 2 characters.\n");
            return EXIT_FAILURE;
        }

        send(sockfd, name, STR_SIZE, 0);

        // password
        printf("Please enter your password (max 30 characters)\n");
        fgets(pswd, STR_SIZE, stdin);

        str_trim_lf(pswd, strlen(pswd));
        trim_trailing_spaces(pswd);

        if (strlen(pswd) > STR_SIZE || strlen(pswd) < 2) {
            printf("Password must be less than 30 and more than 2 characters.\n");
            return EXIT_FAILURE;
        }

        send(sockfd, pswd, STR_SIZE, 0);

        recv(sockfd, status, STR_SIZE, 0);

        if (strcmp(status, LOGIN_ERROR) == 0) {
            printf(LOGIN_ERROR);
            return EXIT_FAILURE;
        } else if (strcmp(status, LOGIN_SUCCESS) == 0) {
            printf(LOGIN_SUCCESS);
        }
    }

    // choosing mode and loading character
    printf("Choose mode: solo or multiplayer? (s/m)\n");
    fgets(mode, STR_SIZE, stdin);
    str_trim_lf(mode, strlen(mode));
    if (strcmp(mode, SOLO) != 0 && strcmp(mode, MULTIPLAYER) != 0) {
        printf("Invalid input. Only s or m are accepted\n");
        return EXIT_FAILURE;
    }
    // send mode (s or m) to server
    send(sockfd, mode, STR_SIZE, 0);

    char info[STR_SIZE];

    recv(sockfd, info, STR_SIZE, 0);
    if (strcmp(info, CREATE_SOLO) == 0 || strcmp(info, CREATE_MULTIPLAYER) == 0) {
        char points[STR_SIZE];
        printf("> ----------------------------------------------------------------------- <\n");
        printf("> Create your character by giving their health,armor,attack and accuracy  <\n");
        printf("> in the form of four integer numbers, comma separated e.g.: 25,15,5,5    <\n");
        printf("> creates a character with health=25, armor=15, attack=5 and accuracy=5   <\n");
        printf("> A character must have a total of 50 points!!                            <\n");
        printf("> ----------------------------------------------------------------------- <\n");

        int totalPoints = 0;
        do {
            totalPoints = 0;
            char temp[STR_SIZE];
            bzero(points, STR_SIZE);
            bzero(temp, STR_SIZE);
            fgets(points, STR_SIZE, stdin);
            strcpy(temp, points);
            char *p = strtok(temp, ",");
            while (p != NULL) {
                if (p != NULL && strcmp(p, "\0") != 0 && strcmp(p, "\n") != 0) {
                    str_trim_lf(p, strlen(p));
                    totalPoints += atoi(p);
                }
                p = strtok(NULL, ",");
            }
            if (totalPoints != 50) {
                printf("Wrong input. Try again.\n");
            }
        } while (totalPoints != 50);

        str_trim_lf(points, strlen(points));
        // send points to server
        send(sockfd, points, STR_SIZE, 0);
    }

    // receive character info
    bzero(info, STR_SIZE);
    recv(sockfd, info, STR_SIZE, 0);
    printf("%s", info);

    // receive level
    char lvl[STR_SIZE];
    recv(sockfd, lvl, STR_SIZE, 0);
    level = atoi(lvl);

    // if playing multiplayer
    if (strcmp(mode, MULTIPLAYER) == 0) {
        char msg[STR_SIZE];
        recv(sockfd, msg, STR_SIZE, 0);
        printf("%s", msg);
        if (strcmp(msg, WAIT_OTHERS) == 0) {
            recv(sockfd, msg, STR_SIZE, 0);
            printf("%s", msg);
        }
    }

    printf("\n");
    printf(">>                        <<\n");
    printf(">>           GO           <<\n");
    printf(">>                        <<\n");
    printf("\n");

    pthread_t send_msg_thread;
    if (pthread_create(&send_msg_thread, NULL, (void *) send_msg_handler, NULL) != 0) {
        printf("ERROR: pthread\n");
    }

    pthread_t recv_msg_thread;
    if (pthread_create(&recv_msg_thread, NULL, (void *) recv_msg_handler, NULL) != 0) {
        printf("ERROR: pthread\n");
    }

    while (1) {
        if (flag) {
            printf("\nBye\n");
            break;
        }
    }
    close(sockfd);

    return EXIT_SUCCESS;
}
