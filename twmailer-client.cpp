#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <ldap.h>
#include "ldapAuthSrc/mypw.h"

//commands
#define SEND 1
#define LIST 2
#define READ 3
#define DEL 4
#define QUIT 5
#define ERROR 6
#define LOGIN 7

int stringCommandToInt(std::string input); //enables switch case for commands

int create_socket = -1;
std::string stringBuffer;
std::string input;

//reads line and adds it to stringBuffer
void getLineToBuffer();

//before sending the actual message, another message containing the size of the actual message is sent,
//so that the server can allocate memory for the message and messages are not limited in size
//by a fixed buffer
void sendMessage(); //sends message from stringBuffer to server
void receiveMessage(); //receives message from server and writes it to stringBuffer

int main(int argc, char *argv[]) {

    
    if(argc < 3){
        fprintf(stderr, "Usage: %s <ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in address;

    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("Socket error");
        exit(EXIT_FAILURE);
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; // IPv4
    address.sin_port = htons(std::stoi(argv[2]));
    inet_aton(argv[1], &address.sin_addr);

    if (connect(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("Connect error - no server available");
        exit(EXIT_FAILURE);
    }

    printf("Connection with server (%s) established\n", inet_ntoa(address.sin_addr));

    receiveMessage(); //receive Message from Server and copy message to stringBuffer
    std::cout << "<< " << stringBuffer << "\n";

    //main loop
    //creates message from input, sends it to server, then waits for response
    while(true) {

        printf("Enter command:\n>> ");
        getline(std::cin, input);
        stringBuffer = input + "\n";

        switch (stringCommandToInt(input)) {
            
            case LOGIN:
                printf("Enter username\n>> ");
                getLineToBuffer();
                stringBuffer += getpass() + "\n";
                break;
            case SEND:
                printf("Enter receiver (max. 8 chars):\n>> ");
                getLineToBuffer();
                printf("Enter subject (max. 80 chars):\n>> ");
                getLineToBuffer();
                printf("Enter message (end message with '.'):\n");
                do{
                    printf(">> ");
                    getLineToBuffer();
                }while(input != ".");
                break;

            case LIST:
                break;

            case READ:
                printf("Enter message number:\n>> ");
                getLineToBuffer();
                break;

            case DEL:
                printf("Enter message number:\n>> ");
                getLineToBuffer();
                break;

            case QUIT:
                break;

            default:
                printf("Invalid command - please try again!\n");
                continue;
        }

        //send Message from stringBuffer to server
        sendMessage();

        if (stringBuffer == "QUIT\n") {
            exit(EXIT_SUCCESS);
        }

        receiveMessage(); //receive Message from Server and copy message to stringBuffer
        std::cout << "<< " << stringBuffer << "\n";
    }

    return 0;
    exit(EXIT_SUCCESS);   
}

void sendMessage(){
    const uint32_t  stringLength = htonl(stringBuffer.length());
    int bytesSent = -1;

    //sends length of upcoming message first
    //set MSG_NOSIGNAL to ignore SIGPIPE error when socket is disconnected (this is handled when recv is called later)
    bytesSent = send(create_socket, &stringLength, sizeof(uint32_t), MSG_NOSIGNAL);

    if(bytesSent == -1){
        if(errno == EPIPE){
            printf("Error - Server closed remote socket\n");
        } else {
            perror("send error");
        }
        printf("Stopping client...\n");
        exit(EXIT_FAILURE);
    };

    if(bytesSent != sizeof(uint32_t)){
        printf("Error - could not send length of message.\n");
        exit(EXIT_FAILURE);
    };

    //sends actual message

    int bytesLeft = stringBuffer.length();
    int index = 0;
    bytesSent = -1;

    while(bytesLeft > 0){
        
        bytesSent = send(create_socket, &stringBuffer.data()[index], bytesLeft, MSG_NOSIGNAL);

        if(bytesSent == -1){
            if(errno == EPIPE){
                printf("Error - Server closed remote socket\n");
            } else {
                perror("send error");
            }
            printf("Stopping client...\n");
            exit(EXIT_FAILURE);
        };

        bytesLeft -= bytesSent;
        index += bytesSent;
    }
};

void receiveMessage(){

    //first we receive length of upcoming message
    uint32_t  lengthOfMessage;
    uint32_t bytesReceived = -1;
    bytesReceived = recv(create_socket, &lengthOfMessage, sizeof(uint32_t),0);
    if (bytesReceived == (unsigned)-1) {
        perror("recv error");
        exit(EXIT_FAILURE);
    }
    if (bytesReceived == (unsigned)0) {
        printf("Server closed remote socket\n"); // ignore error
        exit(EXIT_FAILURE);
    }
    if (bytesReceived != sizeof(uint32_t)) {
        printf("Error - could not receive length of message.\n");
        exit(EXIT_FAILURE);
    }
    lengthOfMessage = ntohl(lengthOfMessage);

    //now we receive actual message

    std::vector<char> receiveBuffer;
    receiveBuffer.resize(lengthOfMessage); //allocate memory for message
    
    bytesReceived = -1;

    //MSG_WAITALL is set so recv waits until entire message is received
    bytesReceived = recv(create_socket, receiveBuffer.data(), lengthOfMessage, MSG_WAITALL);
    if (bytesReceived == (unsigned)-1) {
        perror("recv error");
        exit(EXIT_FAILURE);
    }
    if (bytesReceived == (unsigned)0) {
        printf("Server closed remote socket\n");
        exit(EXIT_FAILURE);
    }
    if (bytesReceived != lengthOfMessage) {
        printf("Received message is shorter than message size.\n");
        exit(EXIT_FAILURE);
    }

    //load message into stringBuffer
    stringBuffer.assign(receiveBuffer.data(), receiveBuffer.size());
}

void getLineToBuffer(){
    getline(std::cin, input);
    stringBuffer += input + "\n";
}

int stringCommandToInt(std::string input){
    if (input == "SEND") {
        return SEND;
    }
    
    if (input == "LIST") {
        return LIST;
    }

    if (input == "READ") {
        return READ;
    }

    if (input == "DEL") {
        return DEL;
    }

    if (input == "QUIT") {
        return QUIT;
    }

    if (input == "LOGIN") {
        return LOGIN;
    }

    return ERROR;
}