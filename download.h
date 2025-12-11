#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>
#include <termios.h>
#include <netdb.h>

#define FTP_PORT 21
#define BUFFER 1024

#define SV_READY4AUTH           220
#define SV_READY4PASS           331
#define SV_LOGINSUCCESS         230
#define SV_PASSIVE              227
#define SV_READY4TRANSFER       150
#define SV_TRANSFER_COMPLETE    226
#define SV_GOODBYE              221

#define AT                  "@"
#define BAR                 "/"
#define HOST_REGEX          "%*[^/]//%[^/]"
#define HOST_AT_REGEX       "%*[^/]//%*[^@]@%[^/]"
#define RESOURCE_REGEX      "%*[^/]//%*[^/]/%s"
#define USER_REGEX          "%*[^/]//%[^:/]"
#define PASS_REGEX          "%*[^/]//%*[^:]:%[^@\n$]"
#define RESPCODE_REGEX      "%d"
#define PASSIVE_REGEX       "%*[^(](%d,%d,%d,%d,%d,%d)%*[^\n$)]"


#define DEFAULT_USER        "anonymous"
#define DEFAULT_PASSWORD    "password"



struct URL {
    char host[MAX_LENGTH];
    char resource[MAX_LENGTH];
    char file[MAX_LENGTH];
    char user[MAX_LENGTH];
    char password[MAX_LENGTH];
    char ip[MAX_LENGTH];
};

typedef enum {
    START,
    SINGLE,
    MULTIPLE,
    END
} ResponseState;


int parse(char *input, struct URL *url);

int createSocket(char *ip, int port);

int authConn(const int socket, const char *user, const char *pass);

int readResponse(const int socket, char *buffer);

int passiveMode(const int socket, char* ip, int *port);

int requestResource(const int socket, char *resource);

int getResource(const int socketA, const int socketB, char *filename);

int closeConnection(const int socketA, const int socketB);