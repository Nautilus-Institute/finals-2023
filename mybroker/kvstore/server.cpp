#include <stdio.h>
#include <string.h>   
#include <stdlib.h>   
#include <unistd.h>   
#include <signal.h>
#include <arpa/inet.h>

#include "store.hpp"

#define MAX_CONNECTIONS 5
#define BUFFER_SIZE 1024
#define PORT 8902
#define NO_SUCH_KEY "No such key.\n"


int recv_until(int sock_fd, unsigned char stop_char, char* outbuf, int outbuf_size)
{
    size_t buf_size = outbuf_size;
    size_t buf_used = 0;
    char *buf = (char*)malloc(buf_size);

    if (!buf) {
        return 0;
    }

    for (;;) {
        unsigned char ch;
        ssize_t status = read(sock_fd, &ch, 1);

        if (status < 1) {
            // error or connection closed by client
            free(buf);
            return 0;
        }

        if (ch == stop_char) {
            // stop character found, return the string
            break;
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

    memcpy(outbuf, buf, buf_used);
    free(buf);
    return buf_used;
}


void send_str(int sock_fd, char* str)
{
    char* buf = (char*)malloc(strlen(str) + 1);
    strcpy(buf, str);
    buf[strlen(str)] = '\n';
    send(sock_fd, buf, strlen(str) + 1, 0);
    free(buf);
}


void store_value_handler(int sock)
{
    char key[256];
    int key_length;
    char type[256];
    int type_length;
    char value[2048];
    int value_length;

    key_length = recv_until(sock, '\n', key, sizeof(key));
    if (key_length <= 0) {
        return;
    }
    type_length = recv_until(sock, '\n', type, sizeof(type));
    if (type_length <= 0) {
        return;
    }
    value_length = recv_until(sock, '\n', value, sizeof(value));
    if (value_length <= 0) {
        return;
    }

    if (strstr(type, "string") != NULL) {
        key[key_length] = 0;
        value[value_length] = 0;
        store_value(std::string(key), std::string(value));
        send_str(sock, "saved.\n");
    }
}


void load_value_handler(int sock)
{
    char key[256];
    int key_length;
    char type[256];
    int type_length;

    key_length = recv_until(sock, '\n', key, sizeof(key));
    if (key_length <= 0) {
        return;
    }
    type_length = recv_until(sock, '\n', type, sizeof(type));
    if (type_length <= 0) {
        return;
    }

    if (strstr(type, "string") != NULL) {
        key[key_length] = 0;
        std::string value = load_string_value(std::string(key));
        if (value.length() == 0) {
            send_str(sock, NO_SUCH_KEY);
        }
        else {
            char value_str[256] = {0};
            strncpy(value_str, value.c_str(), strlen(value.c_str()));
            value_str[std::min(strlen(value_str), sizeof(value_str))] = '\x00';
            send_str(sock, value_str);
        }
    }
}


int main()
{
    initialize();

    char* port_str = getenv("KVSTORE_PORT");
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
    inet_pton(AF_INET6, "::1", &server.sin6_addr);
    
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
            if ((read_size = recv_until(client_sock, '\n', client_message, BUFFER_SIZE)) > 0 ) {
                client_message[read_size] = '\0'; // Null terminate the message

                if (strstr(client_message, "store_value")) {
                    store_value_handler(client_sock);
                } else if (strstr(client_message, "load_value")) {
                    load_value_handler(client_sock);
                } else if (strcmp(client_message, "remove_value") == 0) {
                    // Implement remove_value functionality here
                    write(client_sock, "Received remove_value command", strlen("Received remove_value command"));
                } else {
                    write(client_sock, "Invalid command", strlen("Invalid command"));
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
