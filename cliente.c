#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h> 
#include <arpa/inet.h>

#define BUFFER_SIZE 8192

int parse_url(const char *url, char *host, int *port, char *path) {
    char url_copy[1024];
    strncpy(url_copy, url, 1023);
    url_copy[1023] = '\0';

    char *p = strstr(url_copy, "://");
    if (!p) {
        fprintf(stderr, "URL inválida: falta 'http://'\n");
        return -1;
    }
    p += 3; 

    char *path_start = strchr(p, '/');
    if (path_start) {
        strncpy(path, path_start, 1023);
        path[1023] = '\0';
        *path_start = '\0';
    } else {
        strcpy(path, "/");
    }

    char *port_start = strchr(p, ':');
    if (port_start) {
        *port_start = '\0'; 
        *port = atoi(port_start + 1);
    } else {
        *port = 80;
    }

    strncpy(host, p, 255);
    host[255] = '\0';

    return 0;
}

const char* get_filename_from_path(const char *path) {
    const char *last_slash = strrchr(path, '/');
    if (!last_slash || *(last_slash + 1) == '\0') {
        return "index.html";
    }
    return last_slash + 1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <URL_completa>\n", argv[0]);
        fprintf(stderr, "Exemplo: %s http://localhost:5050/arquivo.txt\n", argv[0]);
        exit(1);
    }

    const char *url = argv[1];
    char host[256];
    int port;
    char path[1024];
    
    if (parse_url(url, host, &port, path) != 0) {
        exit(1);
    }
    
    const char *filename = get_filename_from_path(path);

    printf("Conectando a %s na porta %d para baixar %s...\n", host, port, path);
    printf("Salvando como: %s\n", filename);

    int sock;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    server = gethostbyname(host);
    if (server == NULL) {
        fprintf(stderr, "Erro: Host não encontrado '%s'\n", host);
        exit(1);
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Erro ao criar socket");
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Erro ao conectar");
        exit(1);
    }

    char request_header[2048];
    sprintf(request_header, 
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n", 
        path, host);
    
    if (send(sock, request_header, strlen(request_header), 0) < 0) {
        perror("Erro ao enviar requisição");
        close(sock);
        exit(1);
    }

    char buffer[BUFFER_SIZE];
    int bytes_received;
    FILE *file = NULL;
    int header_found = 0; 

    while ((bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        if (!header_found) {
            buffer[bytes_received] = '\0';
            char *body_start = strstr(buffer, "\r\n\r\n");

            if (body_start) {
                header_found = 1;
                body_start += 4; 
                if (strstr(buffer, "HTTP/1.1 200 OK") == NULL) {
                    char status_line[100];
                    sscanf(buffer, "%99[^\r\n]", status_line);
                    fprintf(stderr, "Erro do servidor: %s\n", status_line);
                    close(sock);
                    exit(1);
                }
                
                file = fopen(filename, "wb");
                if (!file) {
                    perror("Erro ao abrir arquivo para escrita");
                    close(sock);
                    exit(1);
                }

                size_t body_chunk_len = bytes_received - (body_start - buffer);
                if (body_chunk_len > 0) {
                    fwrite(body_start, 1, body_chunk_len, file);
                }
            }
        } else {
            fwrite(buffer, 1, bytes_received, file);
        }
    }

    if (bytes_received < 0) {
        perror("Erro ao receber dados");
    }

    if (file) {
        fclose(file);
    }
    close(sock);

    if (header_found) {
        printf("Download de '%s' concluído.\n", filename);
    } else {
        fprintf(stderr, "Erro: Resposta HTTP inválida ou vazia recebida.\n");
        remove(filename);
    }

    return 0;
}