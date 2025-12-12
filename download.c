#include "download.h"


/*-------- parse() --------
Parse ftp://[user:pass@]host/resource and fill struct URL
*/ 
int parse(char *input, struct URL *url){
    if (strncmp(input, "ftp://", 6) != 0)   return -1;

    char *p = input + 6;
    char *at = strchr(p, '@');
    char *slash;

    if (at && (slash = strchr(p, '/')) && at<slash){
        char creeds[256];
        size_t clen = (size_t)(at-p);
        if (clen >= sizeof(creeds))  return -1;
        memcpy(creeds, p, clen);
        creeds[clen] = '\0';
        char *colon = strchr(creeds, ':');
        if (!colon) return -1;
        *colon = '\0';
        strncpy(url->user, creeds, MAX_LENGTH-1);
        strncpy(url->password, colon+1, MAX_LENGTH-1);
        p = at + 1;
    }   else    {
        strncpy(url->user, DEFAULT_USER, MAX_LENGTH-1);
        strncpy(url->password, DEFAULT_PASSWORD, MAX_LENGTH-1);
    }

    slash = strchr(p, '/');
    if (!slash){
        strncpy(url->host, p, MAX_LENGTH-1);
        strncpy(url->resource, "/", MAX_LENGTH-1);
        strncpy(url->file, "index", MAX_LENGTH-1);
    }   else    {
        size_t hlen = (size_t)(slash-p);
        if (hlen == 0 || hlen >= MAX_LENGTH)    return -1;
        memcpy(url->host, p, hlen);
        url->host[hlen] = '\0';

        const char *res = slash + 1;
        if (strlen(res) == 0){
            strncpy(url->resource, "/", MAX_LENGTH-1);
            strncpy(url->file, "index", MAX_LENGTH-1);
        }   else    {
            strncpy(url->resource, res, MAX_LENGTH-1);
            char *last = strrchr(url->resource, '/');
            if (last && *(last+1) != '\0') {
                strncpy(url->file, last+1, MAX_LENGTH-1);
            }   else    {
                strncpy(url->file, url->resource, MAX_LENGTH-1);
            }
        }
    }
    if (strlen(url->host) == 0) return -1;
    struct hostent *h = gethostbyname(url->host);
    if (!h){
        herror("gethostbyname");
        return -1;
    }
    strncpy(url->ip, inet_ntoa(*((struct in_addr *) h->h_addr)), MAX_LENGTH-1);

    return 0;
}


/*------- createSocket() -------
Create a TCP Socket and connect to ip:port
*/
int createSocket(char *ip, int port){
    if (!ip)    return -1;
    int sockfd;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        perror("connect");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

int readResponse(const int socket, char *buffer){
    if (!buffer)    return -1;
    memset(buffer, 0, MAX_LENGTH);

    char line[MAX_LENGTH];
    int pos = 0;
    char c;
    int firstCode = 0;
    int multiline = 0;

    while (1) {
        ssize_t r = read(socket, &c, 1);
        if (r <= 0){
            if (r < 0)  perror("read");
            break;
        }
        if (pos < MAX_LENGTH-1) line[pos++] = c;
        if (c == '\n') {
            line[pos] = '\0';
            if (firstCode==0){
                if (isdigit((unsigned char)line[0]) && isdigit((unsigned char)line[1]) && isdigit((unsigned char)line[2])){
                    char codeStr[4] = {line[0], line[1], line[2], '\0'};
                    firstCode = atoi(codeStr);
                    if (line[3] == '-'){
                        multiline = 1;
                    }   else    {
                        strncpy(buffer, line, MAX_LENGTH-1);
                        int rc = firstCode;
                        return rc;
                    }
                }   else    {
                    strncpy(buffer, line, MAX_LENGTH-1);
                }
            }   else    {
                if(isdigit((unsigned char)line[0]) && isdigit((unsigned char)line[1]) && isdigit((unsigned char)line[2]) && line[3]==' '){
                    char codeStr[4] = {line[0], line[1], line[2], '\0'};
                    int codeNow = atoi(codeStr);
                    if (codeNow == firstCode){
                        strncpy(buffer, line, MAX_LENGTH-1);
                        return firstCode;
                    }
                }
            }
            pos = 0;
            memset(line, 0, sizeof(line));
        }
    }
    if (strlen(buffer)>0){
        int code = 0;
        sscanf(buffer, RESPCODE_REGEX, &code);
        return code;
    }
    return -1;
}

int authConn(const int socket, const char *user, const char *pass){
    if (!user || !pass) return -1;
    char buffer[MAX_LENGTH];
    char cmd[MAX_LENGTH];

    snprintf(cmd, sizeof(cmd), "USER %s\r\n", user);
    int code = readResponse(socket, buffer);
    if (code == SV_READY4PASS) {
        snprintf(cmd, sizeof(cmd), "PASS %s\r\n", pass);
        code = readResponse(socket, buffer);
        return code;
    }   else if (code == SV_LOGINSUCCESS)   {
        return SV_LOGINSUCCESS;
    } else {
        return code;
    }
}



int passiveMode(const int socket, char* ip, int *port){
    if (!ip || !port)   return -1;
    char buffer[MAX_LENGTH];
    int code = readResponse(socket, buffer);
    if (code != SV_PASSIVE) return code;

    int h1,h2,h3,h4,p1,p2;
    if (sscanf(buffer, PASSIVE_REGEX, &h1, &h2, &h3, &h4, &p1, &p2) != 6){
        char *p = strchr(buffer, '(');
        if (!p) return -1;
        if (sscanf(p, "(%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) return -1;
    }
    snprintf(ip, MAX_LENGTH, "%d,%d,%d,%d,%d,%d", h1, h2, h3, h4, p1, p2);
    *port = p1 * 256 + p2;
    return code;
}

int requestResource(const int socket, char *resource){
    if (!resource)  return -1;
    char cmd[MAX_LENGTH];
    char buffer[MAX_LENGTH];
    readResponse(socket, buffer);

    snprintf(cmd, sizeof(cmd), "RETR %s\r\n", resource);
    int code = readResponse(socket, buffer);
    return code;
}

int getResource(const int socketA, const int socketB, char *filename){
    if (!filename)  return -1;
    FILE *fp = fopen(filename, "wb");
    char buffer[MAX_LENGTH];
    ssize_t n;
    while ((n = read(socketB, buffer, sizeof(buffer)))>0){
        if (fwrite(buffer, 1, (size_t)n, fp) != (size_t)n){
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    close(socketB);

    int final = readResponse(socketA, buffer);
    return final;
}

int closeConnection(const int socketA, const int socketB){
    char buffer[MAX_LENGTH];
    int code = readResponse(socketA, buffer);
    close(socketA);
    close(socketB);
    if (code == SV_GOODBYE)     return 0;
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct URL url;
    if (parse(argv[1], &url) != 0) {
        fprintf(stderr, "Error parsing URL.\n");
        return EXIT_FAILURE;
    }

    printf("Host: %s\nResource: %s\nFile: %s\nUser: %s\nPassword: %s\nIP: %s\n",
           url.host, url.resource, url.file, url.user, url.password, url.ip);

    int ctrl = createSocket(url.ip, FTP_PORT);
    if (ctrl < 0) {
        fprintf(stderr, "Failed to connect to control socket %s:%d\n", url.ip, FTP_PORT);
        return EXIT_FAILURE;
    }

    char buffer[MAX_LENGTH];
    int code = readResponse(ctrl, buffer);
    if (code != SV_READY4AUTH && code != SV_READY4PASS && code != SV_LOGINSUCCESS) {
        fprintf(stderr, "Unexpected welcome code %d\n", code);
        close(ctrl);
        return EXIT_FAILURE;
    }

    code = authConn(ctrl, url.user, url.password);
    if (code != SV_LOGINSUCCESS) {
        fprintf(stderr, "Authentication failed (code %d)\n", code);
        close(ctrl);
        return EXIT_FAILURE;
    }

    char data_ip[MAX_LENGTH];
    int data_port = 0;
    code = passiveMode(ctrl, data_ip, &data_port);
    if (code != SV_PASSIVE) {
        fprintf(stderr, "PASV failed (code %d)\n", code);
        close(ctrl);
        return EXIT_FAILURE;
    }
    printf("PASV -> %s:%d\n", data_ip, data_port);

    int data = createSocket(data_ip, data_port);
    if (data < 0) {
        fprintf(stderr, "Failed to connect data socket %s:%d\n", data_ip, data_port);
        close(ctrl);
        return EXIT_FAILURE;
    }

    code = requestResource(ctrl, url.resource);
    if (code != SV_READY4TRANSFER && code != SV_TRANSFER_COMPLETE) {
        fprintf(stderr, "RETR failed or unexpected code %d\n", code);
        close(ctrl);
        close(data);
        return EXIT_FAILURE;
    }

    code = getResource(ctrl, data, url.file);
    if (code != SV_TRANSFER_COMPLETE) {
        fprintf(stderr, "Transfer did not complete properly (code %d)\n", code);
        close(ctrl);
        return EXIT_FAILURE;
    }

    if (closeConnection(ctrl, data) != 0) {
        fprintf(stderr, "Error closing connection\n");
        return EXIT_FAILURE;
    }

    printf("Saved file '%s'\n", url.file);
    return EXIT_SUCCESS;
}