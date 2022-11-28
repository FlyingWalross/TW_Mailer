#include <sys/types.h>
#include <sys/file.h>
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
#include <filesystem>
#include <fstream>
#include <regex>
#include <signal.h>
#include <sys/wait.h>
#include <ldap.h>
#include "ldapAuthSrc/ldapAuth.h"
#include <chrono>

#define MAX_FAILED_LOGIN_ATTEMPTS 2
#define IP_BLACKLIST_TIME 60 //in seconds

//commands
#define SEND 1
#define LIST 2
#define READ 3
#define DEL 4
#define QUIT 5
#define ERROR 6
#define LOGIN 7

int stringCommandToInt(std::string functionString); //enables switch case for commands

//directory where the mail data will be stored, passed as argument when starting server
char* dataDirectory;

namespace fs = std::filesystem;
int fileLock; //file descriptor for dataDirectory, used to lock entire filesystem
void lock(); //lock filesystem
void unlock(); //unlock filesystem

void signalHandler(int sig);

int create_socket = -1;
int current_socket = -1;
pid_t pid = -1;

void connectionLogic();

std::string stringBuffer; //gloabl variable for input/output buffer
std::string line;

//before sending the actual message, another message containing the size of the actual message is sent,
//so that the client can allocate memory for the message and messages are not limited in size
//by a fixed buffer
int sendMessage(); //sends message from stringBuffer to client
int receiveMessage(); //receives message from client and writes it to stringBuffer

void mailerLogic(); //main logic for mailer functions
bool checkUsername(std::string &username); //checks if username is valid
bool checkSubject(std::string &subject); //checks if email subject is valid

//functions for the different mailer commands, used in mailerLogic()
void login(std::istringstream &inputString);
void send(std::istringstream &inputString);
void list();
void read(std::istringstream &inputString);
void del(std::istringstream &inputString);

//used for login and session
bool loggedIn = false;
std::string sessionUsername;
std::string clientIP;

bool checkIfIPisBlacklisted();
void addFailedLoginAttempt();
void addIPtoBlacklist();

int main(int argc, char *argv[]) {

    if(argc < 3){
        fprintf(stderr, "Usage: %s <port> <mail-spool-directoryname>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (signal(SIGINT, signalHandler) == SIG_ERR) {
        perror("signal can not be registered");
        exit(EXIT_FAILURE);
    }

    if (signal(SIGUSR1, signalHandler) == SIG_ERR) {
        perror("signal can not be registered");
        exit(EXIT_FAILURE);
    }

    socklen_t addrlen;
    struct sockaddr_in address, cliaddress;
    
    dataDirectory = argv[2];

    //make sure that data directory exists
    fs::path p{dataDirectory};
    create_directory(p); //ok to use even if directory already exists

    p /= "messages";
    create_directory(p);

    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }
    
    int option_value = 1;
    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value)) == -1) {
        perror("set socket options - reuseAddr");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEPORT, &option_value, sizeof(option_value)) == -1) {
        perror("set socket options - reusePort");
        exit(EXIT_FAILURE);
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(std::stoi(argv[1]));

    if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind error");
        exit(EXIT_FAILURE);
    }

    if (listen(create_socket, 5) == -1)
    {
      perror("listen error");
      exit(EXIT_FAILURE);
    }

    printf("Waiting for connections...\n");
    
    while (1)
    {
        addrlen = sizeof(struct sockaddr_in);
        if ((current_socket = accept(create_socket, (struct sockaddr *)&cliaddress, &addrlen)) == -1) {
            perror("accept");
            break;
        }

        if((pid = fork()) == 0){   
            close(create_socket);
            clientIP.assign(inet_ntoa(cliaddress.sin_addr));
            printf("\nClient connected from %s:%d\n", inet_ntoa(cliaddress.sin_addr), ntohs(cliaddress.sin_port));
            printf("Client with will be handled by child process %d\n", getpid());
            connectionLogic();
            kill(getppid(), SIGUSR1); //send custom signal to parent process before exiting child process
            exit(EXIT_SUCCESS);
        } else {
            close(current_socket);
        }
    }

    exit(EXIT_SUCCESS);
}

void connectionLogic(){

    stringBuffer = "Welcome to TWMailer!\n";

    //sends Message from stringBuffer
    if(!sendMessage()){
        return;
    };

    //set up file descriptor for file lock
    if((fileLock = open(fs::path(dataDirectory).string().c_str(), O_DIRECTORY, O_RDWR)) == -1){
        perror("open");
        return;
    };

    //main loop while server is connected to client:
    // 1. server waits for message from client and receives it with receiveMessage() 
    // 2. the message is then processed by mailerLogic(),
    // 3. the answer is sent with sendMessage()
    // 4. repeat
    while(1){

        //receaves message and saves message in stringBuffer
        if(!receiveMessage()){
            return;
        };

        if(stringBuffer == "QUIT\n"){
            printf("\nClient sent QUIT\n");
            return;
        }
        
        //processes input, does logic and writes response to stringBuffer
        mailerLogic();

        if(!sendMessage()){
            return;
        };

    }
    
    return;
}

void mailerLogic(){

    //std::cout << "Received from client: " << *stringBuffer << "\n";

    std::istringstream inputString(stringBuffer);
    stringBuffer.clear();

    std::getline(inputString,line);

    switch (stringCommandToInt(line)) {
        case LOGIN:
            login(inputString);
            break;

        case SEND:
            send(inputString);
            break;

        case LIST:
            list();
            break;

        case READ:
            read(inputString);
            break;

        case DEL:
            del(inputString);
            break;

        case QUIT:
            break;

        case ERROR:
            stringBuffer = "ERROR - Command not recognized by server.";
            break;
    }

}

void login(std::istringstream &inputString){
    
    if(checkIfIPisBlacklisted()){
        sessionUsername.clear();
        loggedIn = false;
        stringBuffer = "ERR\n";
        return;
    }
    
    std::string loginUsername;
    std::string loginPassword;
    
    std::getline(inputString, loginUsername);
    std::getline(inputString, loginPassword);

    //Test accounts for debugging
    if((loginUsername == "test1" || loginUsername == "test2") && loginPassword == "test"){
        printf("Client in child process %d sucessfully logged in as %s\n", getpid(), loginUsername.c_str());
        sessionUsername = loginUsername;
        loggedIn = true;
        stringBuffer = "OK\n";
        return;
    }
    
    //actual authentication with LDAP
    if(LDAPauthenticate(loginUsername, loginPassword)){    
        printf("Client in child process %d sucessfully logged in as %s\n", getpid(), loginUsername.c_str());
        sessionUsername = loginUsername;
        loggedIn = true;
        stringBuffer = "OK\n";
        return;
    }

    addFailedLoginAttempt();

    sessionUsername.clear();
    loggedIn = false;
    stringBuffer = "ERR\n";
}

void send(std::istringstream &inputString){

    if(!loggedIn){
        stringBuffer = "ERR\n";
        return;
    }

    std::string receiver;
    std::string subject;

    std::getline(inputString,receiver);
    std::getline(inputString,subject);

    //check if receiver username is valid (min. 1, max. 8 chars, no special chars)
    if(!checkUsername(receiver)){
        printf("receiver is not valid!\n");
        stringBuffer = "ERR\n";
        return;
    }

    //check if subject is valid (max. 80 chars)
    if(!checkSubject(subject)){
        printf("subject is not valid!\n");
        stringBuffer = "ERR\n";
        return;
    }

    fs::path p{dataDirectory};
    p /= "messages";
    p /= receiver; //add receiver to path

    lock();
         
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
    emailFile << sessionUsername << "\n";
    emailFile << receiver << "\n";
    emailFile << subject << "\n";

    //write rest of message to file
    while(getline (inputString,line)){
        emailFile << line << "\n";
    }

    emailFile.close();

    unlock();

    stringBuffer = "OK\n";
}

void list(){
    
    if(!loggedIn){
        stringBuffer = "ERR\n";
        return;
    }
    
    fs::path p{dataDirectory};
    p /= "messages";
    p /= sessionUsername; //add username to path

    lock();

    if(!fs::exists(p)){
        stringBuffer = "0\n";
        unlock();
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

        stringBuffer += "<" + email.path().filename().string() + "> " + line + "\n";
    }
    unlock();
    stringBuffer.insert(0, std::to_string(numberOfMessages) + "\n"); //write number of messages into first line of stringBuffer
}

void read(std::istringstream &inputString){

    if(!loggedIn){
        stringBuffer = "ERR\n";
        return;
    }
    
    fs::path p{dataDirectory};
    p /= "messages";
    p /= sessionUsername; //add username to path
    
    std::getline(inputString,line);
    p /= line; //add message-id to path

    lock();
    
    if(!fs::exists(p)){
        stringBuffer = "ERR\n";
        unlock();
        return;
    }

    std::ifstream emailFile(p.string()); 
    
    if (emailFile.is_open()) {
        
        stringBuffer += "OK\n";
        
        while(getline (emailFile,line)){
            stringBuffer += line + "\n";
        }

        emailFile.close();
    }

    unlock();
}

void del(std::istringstream &inputString){

    if(!loggedIn){
        stringBuffer = "ERR\n";
        return;
    }
    
    fs::path p{dataDirectory};
    p /= "messages";
    p /= sessionUsername; //add username to path
    
    std::getline(inputString,line);
    p /= line; //add message-id to path

    lock();
    
    if(!fs::exists(p)){
        stringBuffer = "ERR\n";
        unlock();
        return;
    }

    fs::remove(p);

    unlock();

    stringBuffer += "OK\n";
}

int stringCommandToInt(std::string functionString){
    if (functionString == "SEND") {
        return SEND;
    }
    
    if (functionString == "LIST") {
        return LIST;
    }

    if (functionString == "READ") {
        return READ;
    }

    if (functionString == "DEL") {
        return DEL;
    }

    if (functionString == "QUIT") {
        return QUIT;
    }

    if (functionString == "LOGIN") {
        return LOGIN;
    }

    return ERROR;
}

int sendMessage(){

    const uint32_t  stringLength = htonl(stringBuffer.length());
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

    int bytesLeft = stringBuffer.length();
    int index = 0;
    bytesSent = -1;

    while(bytesLeft > 0){
        
        bytesSent = send(current_socket, &stringBuffer.data()[index], bytesLeft, MSG_NOSIGNAL);
        
        if(bytesSent == -1){
            perror("send error");
            return false;
        };

        bytesLeft -= bytesSent;
        index += bytesSent;
    }

    return true;
}

int receiveMessage(){

    //first we receive length of upcoming message
    uint32_t  lengthOfMessage;
    uint32_t bytesReceived = -1;
    bytesReceived = recv(current_socket, &lengthOfMessage, sizeof(uint32_t),0);
    if (bytesReceived == (unsigned)-1) {
        perror("recv error");
        return false;
    }
    if (bytesReceived == (unsigned)0) {
        printf("\nClient closed remote socket\n");
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
    if (bytesReceived == (unsigned)-1) {
        perror("recv error");
        return false;
    }
    if (bytesReceived == (unsigned)0) {
        printf("\nClient closed remote socket\n");
        return false;
    }
    if (bytesReceived != lengthOfMessage) {
        printf("Received message is shorter than message size.\n");
        return false;
    }

    //load message into stringBuffer
    stringBuffer.assign(receiveBuffer.data(), receiveBuffer.size());

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

void signalHandler(int sig) {

    if(sig == SIGUSR1){
        pid_t cpid;
        int status;

        if((cpid = wait(&status)) > 0){ //waits for child process to exit
            printf("Child process with id %d exited with code %d\n", cpid, status);
        }
        return;
    }
    
    if(sig == SIGINT){

        if(pid == 0){
            exit(EXIT_SUCCESS);
        }

        printf("\nStopping server...\n");
        pid_t cpid;
        int status;
        while((cpid = wait(&status)) > 0){ //waits for all child processes to exit
            printf("Child process with id %d exited with code %d\n", cpid, status);
        };

        exit(EXIT_SUCCESS);

    }

    exit(sig);
}

void lock(){ 
    if(flock(fileLock, LOCK_EX) != 0){
        perror("flock");
        exit(EXIT_FAILURE);
    };
}

void unlock(){ 
    if(flock(fileLock, LOCK_UN) != 0){
        perror("flock");
        exit(EXIT_FAILURE);
    };
}

bool checkIfIPisBlacklisted(){
    
    fs::path p{dataDirectory};
    p /= "blacklistedIPs";
    p /= clientIP;

    lock();
    
    if(!fs::exists(p)){ //if ip is already blacklisted
        unlock();
        return false;
    }
    
    std::string line;
    std::ifstream ipFile(p); 
    getline (ipFile, line);
    ipFile.close();
    int timeOfBlacklist = stoi(line);
    const auto clockNow = std::chrono::system_clock::now();
    int currenttime = (int)std::chrono::duration_cast<std::chrono::seconds>(clockNow.time_since_epoch()).count();
    if((currenttime - timeOfBlacklist) > IP_BLACKLIST_TIME){
            fs::remove(p);
            unlock();
            return false;
    }

    printf("\nLogin attempt from blacklisted ip: %s\n", clientIP.c_str());
    printf("IP is blocked for %d more seconds\n", IP_BLACKLIST_TIME - (currenttime - timeOfBlacklist));

    unlock();
    return true;
}

void addFailedLoginAttempt(){
    
    printf("\nFailed login attempt on ip: %s\n", clientIP.c_str());
    
    fs::path p{dataDirectory};
    p /= "failedLoginAttempts";

    lock();

    create_directory(p); //ok to use even if directory already exists
    
    p /= clientIP;

    if(fs::exists(p)){ //if ip already has failed attempts
        std::ifstream ipFileRead(p); 
        std::string line;
        getline (ipFileRead, line);
        int numberOfFailedAttempts = stoi(line);
        if(numberOfFailedAttempts >= MAX_FAILED_LOGIN_ATTEMPTS){
            addIPtoBlacklist();
            fs::remove(p);
            unlock();
            return;
        }
        ipFileRead.close();

        std::ofstream ipFileWrite(p);
        ipFileWrite << numberOfFailedAttempts + 1 << "\n";
        ipFileWrite.close();
        unlock();
        return;
    }

    //if file doesn't exist -> first failed attempt
    //create file and write data to file
    std::ofstream ipFile(p);
    ipFile << 1 << "\n";

    ipFile.close();

    unlock();

}

void addIPtoBlacklist(){

    printf("Client with ip %s had more than %d login attempts and will be blacklisted for %d seconds\n", clientIP.c_str(), MAX_FAILED_LOGIN_ATTEMPTS, IP_BLACKLIST_TIME);
    
    fs::path p{dataDirectory};
    p /= "blacklistedIPs";

    lock();

    create_directory(p); //ok to use even if directory already exists
    
    p /= clientIP;

    if(fs::exists(p)){ //if ip is already blacklisted
        unlock();
        return;
    }

    //if ip isn't blacklisted
    //create file and write current timestamp
    const auto clockNow = std::chrono::system_clock::now();
    std::ofstream ipFile(p);
    ipFile << std::chrono::duration_cast<std::chrono::seconds>(clockNow.time_since_epoch()).count() << '\n';
    ipFile.close();

    unlock();

    return;
}