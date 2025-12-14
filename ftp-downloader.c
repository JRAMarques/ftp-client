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
#define DEFAULT_PASSWORD    "anonymous"

struct FTPURL {
    char user[64];
    char password[64];
    char host[128];
    char path[256];
};


int parse_ftp(char* url, struct FTPURL* result){
    if (!url)   return -1;
    if (strncmp(url, "ftp://", 6) != 0){
        fprintf(stderr, "Invalid URL\n");
        return -1;
    }

    memset(result, 0, sizeof(struct FTPURL));
    strcpy(result->user, DEFAULT_USER);
    strcpy(result->password, DEFAULT_PASSWORD);

    const char* ptr = url+6;   //ftp://

    // user:password@
    char *at = strchr(ptr, '@');
    char *colon = strchr(ptr, ':');
    char *slash = strchr(ptr, '/');
    if (!slash){
        fprintf(stderr, "Invalid URL\n");
        return -1;
    }


    if (at && colon && colon < at && at < slash) {
        // user:password@host/path
        strncpy(result->user, ptr, colon - ptr);
        result->user[colon - ptr] = '\0';

        strncpy(result->password, colon+1, at-colon-1);
        result->password[at-colon-1] = '\0';

        if(strlen(result->user) == 0)
            strcpy(result->user, DEFAULT_USER);
        if(strlen(result->password) == 0)
            strcpy(result->password, DEFAULT_PASSWORD);

        ptr = at+1;

    } 

    strncpy(result->host, ptr, slash-ptr);
    result->host[slash-ptr]='\0';

    strcpy(result->path, slash+1);

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

int readResponse(int socket, char *response, size_t size){
    FILE *fp = fdopen(dup(socket), "r");
    char line[512];
    int code = 0;
    
    if (!fp)    return -1;

    response[0] = '\0';

    while(fgets(line, sizeof(line), fp)){
        strncat(response, line, size-strlen(response)-1);

        if(isdigit(line[0]) && isdigit(line[1]) && isdigit(line[2]) && 
        line[3] == ' '){
            code=atoi(line);
            break;
        }
    }
    fclose(fp);
    return code;
}

int authConnection(const int socketfd, struct FTPURL url){
    char response[MAX_LENGTH];
    char cmd[300];
    int code;

    snprintf(cmd, sizeof(cmd), "USER %s\r\n", url.user);
    write(socketfd, cmd, strlen(cmd));

    readResponse(socketfd,response, sizeof(response));
    printf("%s", response);

    if(strncmp(response, "230", 3) == 0)  return 0;

    if(strncmp(response, "331", 3) != 0){
        fprintf(stderr, "USER failed: %s\n", response);
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "PASS %s\r\n", url.password);
    write(socketfd, cmd, strlen(cmd));

    readResponse(socketfd, response, sizeof(response));
    printf("%s", response);

    if (strncmp(response, "230", 3) != 0){
        fprintf(stderr, "PASS failed: %s\n",response);
        return -1;
    }

    return 0;
}


int pasvMode(const int socket, char *ip, int *port){
    if (!ip || !port) return -1;
    char response[MAX_LENGTH];
    if (write(socket, "PASV\r\n", 6) < 0) { perror("write PASV"); return -1; }
    int code = readResponse(socket, response, sizeof(response));
    if (code != 227)     { fprintf(stderr, "PASV failed: %s\n", response); return -1; }

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
    if (code!=150 && code!=125) {
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

    char response[MAX_LENGTH];
    ssize_t bytes;

    while ((bytes = readResponse(socketB, response, sizeof(response))) > 0) {
        fwrite(response,1,bytes,file);
    }

    if (bytes < 0){
        perror("read data socket");
        fclose(file);
        close(socketB);
        return -1;
    }

    fclose(file);
    close(socketB);

    char buffer[MAX_LENGTH];
    int code = readResponse(socketA, buffer, sizeof(buffer));

    if (code!=226){
        fprintf(stderr, "Transfer failed: %s",buffer);
        return -1;
    }
    return 0;
}

int closeConnection(const int socketCtrl){
    char response[MAX_LENGTH];
    if (write(socketCtrl, "QUIT\r\n", 6) < 0){
        perror("write QUIT");
        return -1;
    }
    int code = readResponse(socketCtrl,response,sizeof(response));
    close(socketCtrl);
    return (code!=221) ? 0 : -1;
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

    char response[MAX_LENGTH];
    readResponse(socketA,response, sizeof(response));
    printf("%s", response);

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

    printf("Passive Data Channel: %s:%d\n", dataIP, dataPort);        

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

    if (getResource(socketA, socketB, filename) == 0){
        printf("Download successful: %s\n", filename);
    } else {
        fprintf(stderr, "Download failed\n");
    }

    closeConnection(socketA);
    return 0;
}
