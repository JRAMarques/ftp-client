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
#include <ctype.h>

#define FTP_PORT 21
#define MAX_LENGTH 1024

#define DEFAULT_USER        "anonymous"
#define DEFAULT_PASSWORD    "password"

struct FTPURL {
    char user[64];
    char password[64];
    char host[128];
    char path[256];
};

int parse_ftp(char* url, struct FTPURL* result){
    if (!url)   return -1;
    if (strncmp(url, "ftp://", 6) != 0){
        return -1;
    }

    memset(result, 0, sizeof(struct FTPURL));
    strcpy(result->user, DEFAULT_USER);
    strcpy(result->password, DEFAULT_PASSWORD);

    const char *ptr = url + 6;

    // user:password@
    char *at = strchr(ptr, '@');
    char *slash = strchr(ptr, '/');
    if (!slash) return -1;

    if (at && at < slash) {
        // user:password@host/path
        char credentials[128];
        strncpy(credentials, ptr, at - ptr);
        credentials[at-ptr] = '\0';

        char *colon = strchr(credentials, ':');
        if (colon) {
            *colon = '\0';
            strcpy(result->user, credentials);
            strcpy(result->password, colon+1);
        }   else {
            strcpy(result->user, credentials);
        }
        ptr = at + 1;
    }

    strncpy(result->host, ptr, slash - ptr);
    result->host[slash-ptr] ='\0';
    strcpy(result->path, slash+1);

    //incluir a outra parte

    return 0;
}

int createSocket(char *ip, int port){
    if (!ip)    return -1;

    int sock_fd;
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr(ip);

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0){
        perror("socket() failed");
        return -1;
    }

    if (connect(sock_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0){
        perror("connect() failed");
        return -1;
    }
    return sock_fd;
}

int readResponse(const int socket, char *response, size_t maxLen){
    if (!response)  return -1;
    int n = read(socket, response, maxLen-1);
    if (n > 0)  response[n] = '\0';
    return n;
}

int authConnection(const int socketfd, struct FTPURL url){
    char buffer[MAX_LENGTH];
    char cmd[300];
    int code;

    snprintf(cmd, sizeof(cmd), "USER %s\r\n", url.user);
    write(socketfd, cmd, strlen(cmd));

    readResponse(socketfd, buffer, sizeof(buffer));
    printf("%s", buffer);

    if(strncmp(buffer, "230", 3) == 0)  return 0;

    if(strncmp(buffer, "331", 3) != 0){
        fprintf(stderr, "USER failed: %s\n", buffer);
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "PASS %s\r\n", url.password);
    write(socketfd, cmd, strlen(cmd));

    readResponse(socketfd, buffer, sizeof(buffer));
    printf("%s", buffer);

    if (strncmp(buffer, "230", 3) != 0){
        fprintf(stderr, "PASS failed: %s\n", buffer);
        return -1;
    }

    return 0;
}


int pasvMode(const int socket, char *ip, int *port){
    if (!ip || !port) return -1;
    char response[MAX_LENGTH];
    if (write(socket, "PASV\r\n", 6) < 0) { perror("write PASV"); return -1; }
    int code = readResponse(socket, response, sizeof(response));
    if (strncmp(response, "227", 3) != 0) { fprintf(stderr, "PASV failed: %s\n", response); return -1; }

    int h1,h2,h3,h4,p1,p2;
    char *p = strchr(response, '(');
    if (!p) return -1;
    if (sscanf(p+1, "%d,%d,%d,%d,%d,%d", &h1,&h2,&h3,&h4,&p1,&p2) != 6) return -1;
    sprintf(ip, "%d.%d.%d.%d", h1,h2,h3,h4);
    *port = p1*256 + p2;
    return 0;
}

int requestResource(const int socket, char *resource){
    if (!resource)  return -1;
    char cmd[MAX_LENGTH];
    char response[MAX_LENGTH];

    snprintf(cmd, sizeof(cmd), "RETR %s\r\n", resource);

    if(write(socket, cmd, strlen(cmd)) < 0){
        perror("write RETR");
        return -1;
    }

    int code = readResponse(socket, response, sizeof(response));
    if (strncmp(response, "150", 3) != 0 &&
        strncmp(response, "125", 3) != 0) {
        fprintf(stderr, "RETR failed: %s", response);
        return -1;
    }
    return 0;
}

int getResource(const int socketA, const int socketB, char *filename){
    if (!filename)  return -1;

    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("open");
        return -1;
    }

    char buffer[MAX_LENGTH];
    ssize_t bytes;

    while ((bytes = read(socketB, buffer, sizeof(buffer))) > 0) {
        fwrite(buffer,1,bytes,file);
    }

    if (bytes < 0){
        perror("read data socket");
        fclose(file);
        close(socketB);
        return -1;
    }

    fclose(file);
    close(socketB);

    char response[MAX_LENGTH];
    int code = readResponse(socketA, response, sizeof(response));

    if (strncmp(response, "226", 3) != 0){
        fprintf(stderr, "Transfer failed: %s", response);
        return -1;
    }
    return 0;
}

int closeConnection(const int socketCtrl){
    char buffer[MAX_LENGTH];
    if (write(socketCtrl, "QUIT\r\n", 6) < 0){
        perror("write QUIT");
        return -1;
    }
    int code = readResponse(socketCtrl, buffer, sizeof(buffer));
    close(socketCtrl);
    return (strncmp(buffer, "221", 3) == 0) ? 0 : -1;
}

int main(int argc, char *argv[]){
    if (argc != 2){
        printf("Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n", argv[0]);
        return -1;
    }

    struct FTPURL url; 
    if (parse_ftp(argv[1], &url) != 0){
        fprintf(stderr, "Invalid FTP URL\n");
        return -1;
    }

    struct hostent *h;

    if ((h = gethostbyname(url.host)) == NULL){
        herror("gethostbyname()");
        return -1;
    }

    char *ip = inet_ntoa(*(struct in_addr *)h->h_addr_list[0]);
    int socketA=createSocket(ip, FTP_PORT);
    if (socketA < 0)    return -1;

    char buffer[MAX_LENGTH];
    readResponse(socketA, buffer, sizeof(buffer));
    printf("%s", buffer);

    if(authConnection(socketA, url) != 0){
        printf("auth error\n");
        close(socketA);
        return -1;
    }

    char dataIP[64];
    int dataPort;
    if (pasvMode(socketA, dataIP, &dataPort) != 0){
        fprintf(stderr, "PASV failed\n");
        closeConnection(socketA);
        return -1;
    }

    int socketB = createSocket(dataIP, dataPort);
    if (socketB < 0){
        closeConnection(socketA);
        return -1;
    }

    if (requestResource(socketA, url.path) != 0){
        fprintf(stderr, "RETR failed\n");
        close(socketB);
        closeConnection(socketA);
        return -1;
    }

    char *filename = strrchr(url.path, '/');
    filename = (filename) ? filename + 1 : url.path;

    if (getResource(socketA, socketB, filename) != 0) {
        fprintf(stderr, "Download failed\n");
    }

    closeConnection(socketA);
    return 0;
}
