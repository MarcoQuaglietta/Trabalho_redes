#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

#define BUFFER_SIZE 8192
#define MAX_PATH_SIZE 4096

const char* get_filename_from_filepath(const char *path) {
    const char *last_slash = strrchr(path, '/');
    if (last_slash) {
        return last_slash + 1;
    }
    return path;
}

void error(int client_socket, int status_code, const char *status_msg) {
    char header[1024];
    char body[1024];

    sprintf(body, "<html><body><h1>%d %s</h1></body></html>", status_code, status_msg);
    sprintf(header, 
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n", 
        status_code, status_msg, strlen(body));

    send(client_socket, header, strlen(header), 0);
    send(client_socket, body, strlen(body), 0);
}

const char* get_mime_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/txt";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".txt") == 0) return "text/plain";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".pdf") == 0) return "application/pdf";
    if (strcmp(ext, ".webp") == 0) return "image/webp";
    return "application/octet-stream";
}

void list_directory(int client_socket, const char *dir_path, const char *request_path) {
    char body[BUFFER_SIZE * 4]; 
    char entry_path[MAX_PATH_SIZE];
    char header[1024];
    struct dirent *entry;
    DIR *dir = opendir(dir_path);

    if (!dir) {
        error(client_socket, 404, "Not Found");
        return;
    }

    sprintf(body, "<html><head><title>Index of %s</title></head><body>"
                  "<h1>Index of %s</h1><hr><ul>", request_path, request_path);

    if (strcmp(request_path, "/") != 0) {
        strcat(body, "<li><a href=\"../\">../</a></li>\n");
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue; 

        snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, entry->d_name);
        struct stat entry_stat;
        stat(entry_path, &entry_stat);

        char *display_name = entry->d_name;
        char link_name[MAX_PATH_SIZE];
        
        if (S_ISDIR(entry_stat.st_mode)) {
            snprintf(link_name, sizeof(link_name), "%s/", entry->d_name);
        } else {
            strncpy(link_name, entry->d_name, sizeof(link_name));
        }

        char line[MAX_PATH_SIZE + 100];
        sprintf(line, "<li><a href=\"%s\">%s</a></li>\n", link_name, display_name);
        strcat(body, line);
    }
    closedir(dir);

    strcat(body, "</ul><hr></body></html>");

    sprintf(header, 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n", 
        strlen(body));
    
    send(client_socket, header, strlen(header), 0);
    send(client_socket, body, strlen(body), 0);
}

void file(int client_socket, const char *file_path) {
    FILE *file = fopen(file_path, "rb"); 
    if (!file) {
        error(client_socket, 404, "Not Found");
        return;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    const char *mime_type = get_mime_type(file_path);
    const char *filename = get_filename_from_filepath(file_path); 
    char header[1024];


    sprintf(header, 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Content-Disposition: inline; filename=\"%s\"\r\n" 
        "Connection: close\r\n"
        "\r\n", 
        mime_type, file_size, filename); 
    send(client_socket, header, strlen(header), 0);

    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (send(client_socket, buffer, bytes_read, 0) < 0) {
            perror("Erro ao enviar arquivo");
            break;
        }
    }

    fclose(file);
}

void connection(int client_socket, const char *root_dir) {
    char buffer[BUFFER_SIZE];
    char method[16], path[MAX_PATH_SIZE], protocol[16];
    char full_path[MAX_PATH_SIZE];
    struct stat path_stat;

    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        close(client_socket);
        return;
    }
    buffer[bytes_received] = '\0';

    if (sscanf(buffer, "%15s %4095s %15s", method, path, protocol) != 3) {
        error(client_socket, 400, "Bad Request");
        close(client_socket);
        return;
    }

    if (strcmp(method, "GET") != 0) {
        error(client_socket, 501, "Not Implemented");
        close(client_socket);
        return;
    }

    if (strstr(path, "..")) {
        error(client_socket, 403, "Forbidden");
        close(client_socket);
        return;
    }

    sprintf(full_path, "%s%s", root_dir, path);

    if (stat(full_path, &path_stat) == 0 && S_ISDIR(path_stat.st_mode)) {
        if (path[strlen(path) - 1] != '/') {
            strcat(full_path, "/");
        }

        char index_path[MAX_PATH_SIZE];
        sprintf(index_path, "%sindex.html", full_path);

        if (stat(index_path, &path_stat) == 0) {
            file(client_socket, index_path);
        } else {
            list_directory(client_socket, full_path, path);
        }
    } else {
        file(client_socket, full_path);
    }

    close(client_socket);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <porta> <diretorio_raiz>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    const char *root_dir = argv[2];

    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Erro ao criar socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons(port);       

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro de Bind");
        exit(1);
    }

    if (listen(server_socket, 10) < 0) {
        perror("Erro de Listen");
        exit(1);
    }

    printf("Servidor HTTP ouvindo na porta %d, servindo de %s\n", port, root_dir);

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Erro de Accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Conexão aceita de %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        connection(client_socket, root_dir);
    }

    close(server_socket);
    return 0;
}

