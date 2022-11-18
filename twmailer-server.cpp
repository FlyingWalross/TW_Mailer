#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <filesystem>
#include <fstream>
#include <regex>

namespace fs = std::filesystem;

//needed for handling ctrl+c interrupt for shutting down the server
void signalHandler(int sig);

int abortRequested = 0;
int create_socket = -1;
int current_socket = -1;

//directory where the mail data will be stored, passed as argument when starting server
char* dataDirectory;

void socketLogic(int* current_socket);
void closeCreateSocket();
void closeCurrentSocket();


//before sending the actual message, another message containing the size of the actual message is sent,
//so that the client can allocate memory for the message and messages are not limited in size
//by a fixed buffer
int sendMessage(std::string* stringBuffer); //sends message from stringBuffer to client
int receiveMessage(std::string* stringBuffer); //receives message from client and writes it to stringBuffer

void mailerLogic(std::string* stringBuffer); //main logic for mailer functions
bool checkUsername(std::string &username); //checks if username is valid
bool checkSubject(std::string &subject); //checks if email subject is valid

int stringCommandToEnum(std::string functionString); //enables switch case with mailer function in mailerLogic()
enum commands {
    SEND = 1,
    LIST = 2,
    READ = 3,
    DEL = 4,
    QUIT = 5,
    ERROR = 6
};

//function for the different mailer commands, used in mailerLogic()
void send(std::string* stringBuffer, std::istringstream &inputString, std::string &line);
void list(std::string* stringBuffer, std::istringstream &inputString, std::string &line);
void read(std::string* stringBuffer, std::istringstream &inputString, std::string &line);
void del(std::string* stringBuffer, std::istringstream &inputString, std::string &line);

int main(int argc, char *argv[]) {

    if(argc < 3){
        fprintf(stderr, "Usage: %s <port> <mail-spool-directoryname>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    dataDirectory = argv[2];

    socklen_t addrlen;
    struct sockaddr_in address, cliaddress;
    int new_socket = -1;

   if (signal(SIGINT, signalHandler) == SIG_ERR) {
      perror("signal can not be registered");
      return EXIT_FAILURE;
   }

    //make sure that data directory exists
    fs::path p{dataDirectory};
    create_directory(p); //ok to use even if directory already exists

    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error creating socket");
        return EXIT_FAILURE;
    }
    
    int option_value = 1;
    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value)) == -1) {
        perror("set socket options - reuseAddr");
        return EXIT_FAILURE;
    }

    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEPORT, &option_value, sizeof(option_value)) == -1) {
        perror("set socket options - reusePort");
        return EXIT_FAILURE;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(std::stoi(argv[1]));

    if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind error");
        return EXIT_FAILURE;
    }

    if (listen(create_socket, 5) == -1)
    {
      perror("listen error");
      return EXIT_FAILURE;
    }

    while (!abortRequested)
    {
        printf("Waiting for connections...\n");
        addrlen = sizeof(struct sockaddr_in);
        if ((new_socket = accept(create_socket, (struct sockaddr *)&cliaddress, &addrlen)) == -1) {
            if (!abortRequested) {
                perror("accept error");
            }
            break;
        }

        printf("Client connected from %s:%d\n",
                inet_ntoa(cliaddress.sin_addr),
                ntohs(cliaddress.sin_port));
        socketLogic(&new_socket);
        new_socket = -1;
    }

    closeCreateSocket();
    return EXIT_SUCCESS;
}

void socketLogic(int* new_socket){

    current_socket = *new_socket;

    std::string stringBuffer;

    stringBuffer = "Welcome to TWMailer!\n";

    //sends Message from stringBuffer
    if(!sendMessage(&stringBuffer)){
        closeCurrentSocket();
        return;
    };

    //main loop while server is connected to client:
    // 1. server waits for message from client and receives it with receiveMessage() 
    // 2. the message is then processed by mailerLogic(),
    // 3. the answer is sent with sendMessage()
    // 4. repeat
    while(!abortRequested){

        //receaves message and saves message in stringBuffer
        if(!receiveMessage(&stringBuffer)){
            break;
        };

        if(stringBuffer == "QUIT\n"){
            break;
        }
        
        //processes input, does logic and writes response to stringBuffer
        mailerLogic(&stringBuffer);

        if(!sendMessage(&stringBuffer)){
            break;
        };

    }

    closeCurrentSocket();
    return;
}

void mailerLogic(std::string* stringBuffer){

    std::cout << "Received from client: " << *stringBuffer << "\n";

    std::istringstream inputString(*stringBuffer);
    std::string line;
    stringBuffer->clear();

    std::getline(inputString,line);

    switch (stringCommandToEnum(line)) {
        case SEND:
            send(stringBuffer, inputString, line);
            break;

        case LIST:
            list(stringBuffer, inputString, line);
            break;

        case READ:
            read(stringBuffer, inputString, line);
            break;

        case DEL:
            del(stringBuffer, inputString, line);
            break;

        case QUIT:
            break;

        case ERROR:
            *stringBuffer = "ERROR - Command not recognized by server.";
            break;
    }

}

void send(std::string* stringBuffer, std::istringstream &inputString, std::string &line){

    std::string sender;
    std::string receiver;
    std::string subject;

    std::getline(inputString,sender);
    std::getline(inputString,receiver);
    std::getline(inputString,subject);

    //check if sender username is valid (min. 1, max. 8 chars, no special chars)
    if(!checkUsername(sender)){
        printf("username is not valid!\n");
        *stringBuffer = "ERR\n";
        return;
    }

    //check if receiver username is valid (min. 1, max. 8 chars, no special chars)
    if(!checkUsername(receiver)){
        printf("receiver is not valid!\n");
        *stringBuffer = "ERR\n";
        return;
    }

    //check if subject is valid (max. 80 chars)
    if(!checkSubject(subject)){
        printf("subject is not valid!\n");
        *stringBuffer = "ERR\n";
        return;
    }

    fs::path p{dataDirectory};

    p /= receiver; //add receiver to path

    create_directory(p); //ok to use even if directory already exists

    //count number of messages for new message-id
    int highestMessageId = 0;
    for (auto const &email : fs::directory_iterator(p)){
        if(std::stoi(email.path().filename().string()) > highestMessageId){
            highestMessageId = std::stoi(email.path().filename().string());
        }
    }

    p /= std::to_string(highestMessageId + 1); //set path to message-id

    //create file and write data to file
    std::ofstream emailFile(p);
    emailFile << sender << "\n";
    emailFile << receiver << "\n";
    emailFile << subject << "\n";

    //write rest of message to file
    while(getline (inputString,line)){
        emailFile << line << "\n";
    }

    emailFile.close();

    *stringBuffer = "OK\n";
}

void list(std::string* stringBuffer, std::istringstream &inputString, std::string &line){
    
    fs::path p{dataDirectory};

    std::getline(inputString,line);
    p /= line; //add username to path

    //check if username is valid (min. 1, max. 8 chars, no special chars)
    if(!checkUsername(line)){
        printf("username is not valid!\n");
        *stringBuffer = "ERR\n";
        return;
    }

    if(!fs::exists(p)){
        *stringBuffer = "0\n";
        return;
    }

    //count number of messages and write list of messages to stringBuffer
    int numberOfMessages = 0;
    
    for (auto const &email : fs::directory_iterator(p)){
        numberOfMessages++;

        std::ifstream emailFile(email.path().string()); 
        if (emailFile.is_open()) {
            getline (emailFile,line); //skip first to lines to get to subject
            getline (emailFile,line);
            getline (emailFile,line);
            emailFile.close();
        }

        *stringBuffer += "<" + email.path().filename().string() + "> " + line + "\n";
    }
    stringBuffer->insert(0, std::to_string(numberOfMessages) + "\n"); //write number of messages into first line of stringBuffer
}

void read(std::string* stringBuffer, std::istringstream &inputString, std::string &line){

    fs::path p{dataDirectory};

    std::getline(inputString,line);
    p /= line; //add username to path

    //check if username is valid (min. 1, max. 8 chars, no special chars)
    if(!checkUsername(line)){
        printf("username is not valid!\n");
        *stringBuffer = "ERR\n";
        return;
    }
    
    std::getline(inputString,line);
    p /= line; //add message-id to path

    if(!fs::exists(p)){
        *stringBuffer = "ERR\n";
        return;
    }

    std::ifstream emailFile(p.string()); 
    
    if (emailFile.is_open()) {
        
        *stringBuffer += "OK\n";
        
        while(getline (emailFile,line)){
            *stringBuffer += line + "\n";
        }

        emailFile.close();
    }
}

void del(std::string* stringBuffer, std::istringstream &inputString, std::string &line){

    fs::path p{dataDirectory};

    std::getline(inputString,line);
    p /= line; //add username to path

    //check if username is valid (min. 1, max. 8 chars, no special chars)
    if(!checkUsername(line)){
        printf("username is not valid!\n");
        *stringBuffer = "ERR\n";
        return;
    }
    
    std::getline(inputString,line);
    p /= line; //add message-id to path

    if(!fs::exists(p)){
        *stringBuffer = "ERR\n";
        return;
    }

    fs::remove(p);
    *stringBuffer += "OK\n";
}

void closeCreateSocket(){
    if (create_socket != -1)
    {
        if (shutdown(create_socket, SHUT_RDWR) == -1)
        {
            perror("shutdown create_socket");
        }
        if (close(create_socket) == -1)
        {
            perror("close create_socket");
        }
        create_socket = -1;
    }
}

void closeCurrentSocket(){
    if (current_socket != -1)
    {
        if (shutdown(current_socket, SHUT_RDWR) == -1)
        {
            perror("shutdown current_socket");
        }
        if (close(current_socket) == -1)
        {
            perror("close current_socket");
        }
        current_socket = -1;
    }
}

void signalHandler(int sig) {
    if (sig == SIGINT)
    {
        printf("\nStopping server...\n"); // ignore error
        abortRequested = 1;

        closeCreateSocket();
        closeCurrentSocket();

    }
    else
    {
        exit(sig);
    }
}

int stringCommandToEnum(std::string functionString){
    if (functionString == "SEND") {
        return 1;
    }
    
    if (functionString == "LIST") {
        return 2;
    }

    if (functionString == "READ") {
        return 3;
    }

    if (functionString == "DEL") {
        return 4;
    }

    if (functionString == "QUIT") {
        return 5;
    }

    return 6;
}

int sendMessage(std::string* stringBuffer){

    const uint32_t  stringLength = htonl(stringBuffer->length());
    int bytesSent = -1;

    //sends length of upcoming message first
    //set MSG_NOSIGNAL to ignore SIGPIPE error when socket is disconnected (this is handled when recv is called later)
    bytesSent = send(current_socket, &stringLength, sizeof(uint32_t), MSG_NOSIGNAL);

    if(bytesSent == -1){
        perror("send error");
        return false;
    };

    if(bytesSent != sizeof(uint32_t)){
        printf("Error - could not send length of message.\n");
        return false;
    };

    //now sends actual message

    int bytesLeft = stringBuffer->length();
    int index = 0;
    bytesSent = -1;

    while(bytesLeft > 0){
        
        bytesSent = send(current_socket, &stringBuffer->data()[index], bytesLeft, MSG_NOSIGNAL);
        
        if(bytesSent == -1){
            perror("send error");
            return false;
        };

        bytesLeft -= bytesSent;
        index += bytesSent;
    }

    return true;
}

int receiveMessage(std::string* stringBuffer){

    //first we receive length of upcoming message
    uint32_t  lengthOfMessage;
    uint32_t bytesReceived = -1;
    bytesReceived = recv(current_socket, &lengthOfMessage, sizeof(uint32_t),0);
    if (abortRequested) {
        return false;
    }
    if (bytesReceived == (unsigned)-1) {
        perror("recv error");
        return false;
    }
    if (bytesReceived == (unsigned)0) {
        printf("Client closed remote socket\n");
        return false;
    }
    if (bytesReceived != sizeof(uint32_t)) {
        printf("Error - could not receive length of message.\n");
        return false;
    }
    lengthOfMessage = ntohl(lengthOfMessage);

    //now we receive actual message
    
    std::vector<char> receiveBuffer;
    receiveBuffer.resize(lengthOfMessage); //allocate memory for message
    
    bytesReceived = -1;

    //MSG_WAITALL is set so recv waits until entire message is received
    bytesReceived = recv(current_socket, receiveBuffer.data(), lengthOfMessage, MSG_WAITALL);
    if (abortRequested) {
        perror("recv error after aborted");
        return false;
    }
    if (bytesReceived == (unsigned)-1) {
        perror("recv error");
        return false;
    }
    if (bytesReceived == (unsigned)0) {
        printf("Client closed remote socket\n");
        return false;
    }
    if (bytesReceived != lengthOfMessage) {
        printf("Received message is shorter than message size.\n");
        return false;
    }

    //load message into stringBuffer
    stringBuffer->assign(receiveBuffer.data(), receiveBuffer.size());

    return true;
}

bool checkUsername(std::string &username){
    
     if(std::regex_match (username, std::regex("[a-z0-9]{1,8}"))){
         return true;
     }

     return false;
}

bool checkSubject(std::string &subject){
     if(std::regex_match (subject, std::regex(".{0,80}"))){
         return true;
     }

     return false;
}