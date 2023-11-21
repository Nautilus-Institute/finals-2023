#include <stdio.h>
#include <string.h>   
#include <stdlib.h>   
#include <unistd.h>   
#include <signal.h>
#include <arpa/inet.h>

#define MAX_CONNECTIONS 5
#define BUFFER_SIZE 1024
#define PORT 8906

// This program implements the Chicken protocol

int recv_message(int sock_fd, char* outbuf, int outbuf_size)
{
    size_t buf_size = outbuf_size;
    size_t buf_used = 0;
    char *buf = (char*)malloc(buf_size);

    if (!buf) {
        return 0;
    }

    int size;
    read(sock_fd, &size, 4);

    while (buf_used < size) {
        unsigned char ch;
        ssize_t status = read(sock_fd, &ch, 1);

        if (status < 1) {
            // error or connection closed by client
            free(buf);
            return 0;
        }

        // add character to buffer, resizing it if necessary
        if (buf_used == buf_size - 1) {
            buf_size *= 2;
            char *new_buf = (char*)realloc(buf, buf_size);

            if (!new_buf) {
                free(buf);
                return 0;
            }

            buf = new_buf;
        }

        buf[buf_used++] = ch;
    }

    if (buf_used <= (unsigned short)outbuf_size) {
        memcpy(outbuf, buf, buf_used);
    }
    free(buf);
    return buf_used;
}


void send_message(int sock_fd, unsigned char* buf, int size)
{
    write(sock_fd, &size, 4);
    write(sock_fd, buf, size);
}


int get_random_number()
{
    // TODO: Invoke the random number generation service using kvstore 
    return time(NULL);
}


void upload_file_handler(int sock)
{
    // Receive the file content
    char file_name[128] = {0};
    int file_name_size = recv_message(sock, file_name, sizeof(file_name));
    if (file_name_size <= 0) {
        file_name_size = 1;
        file_name[0] = 'a';
        file_name[1] = '\x00';
    }

    char file_content[0x8ffff];
    int file_size = recv_message(sock, file_content, sizeof(file_content));
    if (file_size <= 0) {
        send_message(sock, "FILE TOO SMALL", 6);
        return;
    }

    // no arbitrary file write!
    // VULN: the file name can include question marks
    for (int i = 0; i < file_name_size; ++i) {
        if ((i < file_name_size - 1 && file_name[i] == '.' && file_name[i + 1] == '.') || file_name[i] == '/') {
            send_message(sock, "INVALID SUFFIX", 14);
            return;
        }
    }

    // Generate a file name
    char* file_path[1024] = {0};
    char* local_file_name[1024] = {0};
    sprintf(local_file_name, "%d_%s", get_random_number(), file_name);
    sprintf(file_path, "/tmp/%s", local_file_name);

    // Write file
    FILE *fp = fopen(file_path, "wb");
    if (fp == NULL) {
        send_message(sock, "INVALID PATH", 12);
        return;
    }
    fwrite(file_content, file_size, 1, fp);
    fclose(fp);
    send_message(sock, local_file_name, strlen(local_file_name));
    send_message(sock, "SAVED", 5);
}


int main()
{
    char* port_str = getenv("FILEUP_PORT");
    int port;
    if (!port_str) {
        port = PORT;
    }
    else {
        port = atoi(port_str);
    }

    int socket_desc, client_sock, c;
    struct sockaddr_in6 server, client;
    
    //Create socket
    socket_desc = socket(AF_INET6, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        printf("Could not create socket");
        return 1;
    }
    
    //Prepare the sockaddr_in structure
    server.sin6_family = AF_INET6;
    server.sin6_port = htons(port);
    inet_pton(AF_INET6, "::1", &server.sin6_addr);  // binding to localhost
    
    //Bind
    if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind failed. Error");
        return 1;
    }
    
    //Listen
    listen(socket_desc, MAX_CONNECTIONS);
    
    //Accept incoming connection
    c = sizeof(struct sockaddr_in);
    while((client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c))) {
        if (client_sock < 0) {
            perror("accept failed");
            return 1;
        }
        
        // Create child process
        if (fork() == 0) {
            // Child process
            close(socket_desc); // Child doesn't need the listener
            
            // Receive a message from client
            char client_message[BUFFER_SIZE];
            int read_size;
            if ((read_size = recv_message(client_sock, client_message, BUFFER_SIZE)) > 0 ) {
                client_message[read_size] = '\0'; // Null terminate the message

                if (!strcmp(client_message, "store_file")) {
                    upload_file_handler(client_sock);
                } else if (!strcmp(client_message, "remove_file")) {
                    ;
                    // TODO:
                    // remove_file_handler(client_sock);
                } else {
                    send_message(client_sock, "INVALID COMMAND", 15);
                }
            }
            
            if(read_size == 0) {
                close(client_sock);
            } else if(read_size == -1) {
                perror("recv failed");
            }
            
            exit(0);
        }
        
        signal(SIGCHLD,SIG_IGN);
        // Parent process
        close(client_sock);
    }
    
    return 0;
}
