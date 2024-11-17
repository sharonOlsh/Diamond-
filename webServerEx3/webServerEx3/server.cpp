#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
using namespace std;
#pragma comment(lib, "Ws2_32.lib")
#include <winsock2.h>
#include <string.h>
#include <time.h>

#define BUFFER_SIZE 4096
#define MAX_PERSONS 100

typedef struct {
    int id;
    char name[100];
} Person;

Person personList[MAX_PERSONS];
int personCount = 0;

struct SocketData {
    SOCKET id;
    int recv;
    int send;
    char buffer[BUFFER_SIZE];
    int len;
    time_t timeOut;
};

const int HTTP_PORT = 8080;
const int MAX_SOCKETS = 60;
const int EMPTY = 0;
const int LISTEN = 1;
const int RECEIVE = 2;
const int IDLE = 3;
const int SEND = 4;

SocketData sockets[MAX_SOCKETS] = { 0 };
int socketsCount = 0;

bool addSocket(SOCKET id, int what);
void removeSocket(int index);
void acceptConnection(int index);
void receiveMessage(int index);
void handleRequest(SocketData* socket);
void sendMessage(int index);
char* readHtmlFile(const char* filePath);
char* handleGetRequest(SocketData* socket);
void handlePutRequest(SocketData* socket, const char* responseTemplate);
void handleDeleteRequest(SocketData* socket, const char* responseTemplate);
void handleTraceRequest(SocketData* socket, char* response, const char* responseTemplate, const char* notFoundResponse);
void printListOfPeople(char* responseBody, char* response, const char* responseTemplate);
void updateSocketsTime(SocketData* sockets, int& socketsNum);
Person* getPersonById(int id);
void addOrUpdatePerson(int id, const char* name);
int deletePersonById(int id);

int main() {
    WSAData wsaData;
    if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData)) {
        cout << "HTTP Server: Error at WSAStartup()\n";
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (INVALID_SOCKET == listenSocket) {
        cout << "HTTP Server: Error at socket(): " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverService;
    serverService.sin_family = AF_INET;
    serverService.sin_addr.s_addr = INADDR_ANY;
    serverService.sin_port = htons(HTTP_PORT);

    if (SOCKET_ERROR == bind(listenSocket, (SOCKADDR*)&serverService, sizeof(serverService))) {
        cout << "HTTP Server: Error at bind(): " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    if (SOCKET_ERROR == listen(listenSocket, 5)) {
        cout << "HTTP Server: Error at listen(): " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    addSocket(listenSocket, LISTEN);

    while (true) {
        updateSocketsTime(sockets, socketsCount);
        fd_set waitRecv;
        FD_ZERO(&waitRecv);
        for (int i = 0; i < MAX_SOCKETS; i++) {
            if ((sockets[i].recv == LISTEN) || (sockets[i].recv == RECEIVE))
                FD_SET(sockets[i].id, &waitRecv);
        }

        fd_set waitSend;
        FD_ZERO(&waitSend);
        for (int i = 0; i < MAX_SOCKETS; i++) {
            if (sockets[i].send == SEND)
                FD_SET(sockets[i].id, &waitSend);
        }

        int nfd = select(0, &waitRecv, &waitSend, NULL, NULL);
        if (nfd == SOCKET_ERROR) {
            cout << "HTTP Server: Error at select(): " << WSAGetLastError() << endl;
            WSACleanup();
            return 1;
        }

        for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++) {
            if (FD_ISSET(sockets[i].id, &waitRecv)) {
                nfd--;
                switch (sockets[i].recv) {
                case LISTEN:
                    acceptConnection(i);
                    break;
                case RECEIVE:
                    receiveMessage(i);
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++) {
            if (FD_ISSET(sockets[i].id, &waitSend)) {
                nfd--;
                sendMessage(i);
            }
        }
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}

bool addSocket(SOCKET id, int what) {
    unsigned long flag = 1;
    if (ioctlsocket(id, FIONBIO, &flag) != 0) {
        cout << "HTTP Server: Error at ioctlsocket(): " << WSAGetLastError() << endl;
    }
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].recv == EMPTY) {
            sockets[i].id = id;
            sockets[i].recv = what;
            sockets[i].send = IDLE;
            sockets[i].len = 0;
            sockets[i].timeOut = time(0);
            socketsCount++;
            return true;
        }
    }
    return false;
}

void removeSocket(int index) {
    sockets[index].recv = EMPTY;
    sockets[index].send = EMPTY;
    socketsCount--;
}

void acceptConnection(int index) {
    SOCKET id = sockets[index].id;
    struct sockaddr_in from;
    int fromLen = sizeof(from);

    SOCKET msgSocket = accept(id, (struct sockaddr*)&from, &fromLen);
    if (INVALID_SOCKET == msgSocket) {
        cout << "HTTP Server: Error at accept(): " << WSAGetLastError() << endl;
        return;
    }
    cout << "HTTP Server: Client " << inet_ntoa(from.sin_addr) << ":" << ntohs(from.sin_port) << " is connected." << endl;

    if (addSocket(msgSocket, RECEIVE) == false) {
        cout << "Too many connections, dropped!" << endl;
        closesocket(id);
    }
}

void receiveMessage(int index) {
    SOCKET msgSocket = sockets[index].id;

    int len = sockets[index].len;
    int bytesRecv = recv(msgSocket, &sockets[index].buffer[len], sizeof(sockets[index].buffer) - len, 0);

    if (SOCKET_ERROR == bytesRecv) {
        cout << "HTTP Server: Error at recv(): " << WSAGetLastError() << endl;
        closesocket(msgSocket);
        removeSocket(index);
        return;
    }
    if (bytesRecv == 0) {
        closesocket(msgSocket);
        removeSocket(index);
        return;
    }
    else {
        sockets[index].len += bytesRecv;
        sockets[index].buffer[sockets[index].len] = '\0';
        sockets[index].timeOut = time(0);
        handleRequest(&sockets[index]);
    }
}

char* readHtmlFile(const char* filePath) {

    FILE* file = fopen(filePath, "rb");
    if (file == NULL) {
        perror("Error opening file");
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    char* fileContent = (char*)malloc(fileSize + 1);
    if (fileContent == NULL) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }

    size_t size = fread(fileContent, 1, fileSize, file);

    if (size != fileSize) {
        perror("Error reading file");
        free(fileContent);
        fclose(file);
        return NULL;
    }


    fileContent[size] = '\0';

    fclose(file);
    return fileContent;
}

char* handleGetRequest(SocketData* socket) {

    char* startUrl = strchr(socket->buffer, ' ') + 1;
    char* endUrl = strchr(startUrl, ' ');
    *endUrl = '\0';
    char* body;

    if (strstr(startUrl, "index.html")) {
        char* queryParam = strchr(startUrl, '?');
        if (queryParam != NULL) {
            queryParam++;
            char* lang = strstr(queryParam, "lang=");
            if (lang != NULL) {
                lang += 5;
                if (strncmp(lang, "fr", 2) == 0) {
                    body = readHtmlFile("C:\\temp\\index_fr.html");
                }
                else if (strncmp(lang, "he", 2) == 0) {
                    body = readHtmlFile("C:\\temp\\index_he.html");
                }
                else {
                    body = readHtmlFile("C:\\temp\\index_en.html");
                }
            }
            else {
                body = readHtmlFile("C:\\temp\\index_en.html");
            }
        }
        else {
            body = readHtmlFile("C:\\temp\\index_en.html");
        }
    }
    else {
        body = (char*)malloc(strlen("404 - Not Found") + 1);
        if (body != NULL) {
            strcpy(body, "404 - Not Found");
        }
    }
    return body;
}


Person* getPersonById(int id) {
    for (int i = 0; i < personCount; i++) {
        if (personList[i].id == id) {
            return &personList[i];
        }
    }
    return NULL;
}

void addOrUpdatePerson(int id, const char* name) {
    Person* person = getPersonById(id);
    if (person) {
        strncpy(person->name, name, sizeof(person->name) - 1);
        person->name[sizeof(person->name) - 1] = '\0';
    }
    else if (personCount < MAX_PERSONS) {
        personList[personCount].id = id;
        strncpy(personList[personCount].name, name, sizeof(personList[personCount].name) - 1);
        personList[personCount].name[sizeof(personList[personCount].name) - 1] = '\0';
        personCount++;
    }
}

int deletePersonById(int id) {
    for (int i = 0; i < personCount; i++) {
        if (personList[i].id == id) {
            personList[i] = personList[personCount - 1];
            personCount--;
            return 1;
        }
    }
    return 0;
}

void handleDeleteRequest(SocketData* socket, const char* responseTemplate, char* response, bool* isSuccessful) {
    int id;
    char responseBody[BUFFER_SIZE];
    if (sscanf(socket->buffer, "DELETE /person?id=%d", &id) == 1)
    {
        if (deletePersonById(id)) {
            snprintf(responseBody, BUFFER_SIZE, "DELETE request processed. Deleted person with ID: %d", id);
        }
        else {
            snprintf(responseBody, BUFFER_SIZE, "Person with ID: %d not found", id);
        }
        printListOfPeople(responseBody, response, responseTemplate);
        *isSuccessful = true;
    }
    else
    {
        *isSuccessful = false;
    }
}

void handlePutRequest(SocketData* socket, const char* responseTemplate, char* response, bool* isSuccessful) {
    int id;
    char name[100];

    if (sscanf(socket->buffer, "PUT /person?id=%d&name=%99s", &id, name) == 2)
    {
        addOrUpdatePerson(id, name);
        char responseBody[BUFFER_SIZE];
        snprintf(responseBody, BUFFER_SIZE, "PUT request processed. Updated/Added person with ID: %d, Name: %s", id, name);
        printListOfPeople(responseBody, response, responseTemplate);
        *isSuccessful = true;
    }
    else
    {
        *isSuccessful = false; 
    }
}

void printListOfPeople(char* responseBody, char* response, const char* responseTemplate) {
    strcat(responseBody, "\n\nUpdated list of people:\n");

    for (int i = 0; i < personCount; i++) {
        char personData[100];
        snprintf(personData, sizeof(personData), "ID: %d, Name: %s\n", personList[i].id, personList[i].name);
        strcat(responseBody, personData);
    }
    snprintf(response, BUFFER_SIZE, responseTemplate, "text/plain", (int)strlen(responseBody), responseBody);
}

void handleOptionsRequest(SocketData* socket, char* response) {
    char* startUrl = strchr(socket->buffer, ' ') + 1;
    char* endUrl = strchr(startUrl, ' ');
    *endUrl = '\0';
    if (strstr(startUrl, "index.html")) {
        strcpy(response, "HTTP/1.1 204 No Content\r\nAllow: OPTIONS, GET, HEAD\r\nConnection: close\r\n\r\n");
    }
    else if (strstr(startUrl, "person")) {
        strcpy(response, "HTTP/1.1 204 No Content\r\nAllow: OPTIONS, PUT, DELETE\r\nConnection: close\r\n\r\n");
    }
    else {
        if (strcmp(startUrl, "/") == 0) {
            strcpy(response, "HTTP/1.1 204 No Content\r\nAllow: OPTIONS, POST, TRACE\r\nConnection: close\r\n\r\n");
        }
        else {
            strcpy(response, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        }
    }
    strcpy(socket->buffer, response);
    socket->len = (int)strlen(response);
    socket->send = SEND;
}

void handlePostRequest(SocketData* socket, char* response, const char* responseTemplate, const char* not_found_response)
{
    char* bodyRequest = strstr(socket->buffer, "\r\n\r\n");;
    if (bodyRequest)
    {
        bodyRequest += 4;
    }
    char* urlStart = strchr(socket->buffer, ' ') + 1;
    char* urlEnd = strchr(urlStart, ' ');
    *urlEnd = '\0';
    if (strcmp(urlStart, "/") == 0)
    {
        const char* body = "POST request received";
        cout << "Received POST data: " << bodyRequest << endl;
        int bodyLength = strlen(body);
        snprintf(response, BUFFER_SIZE, responseTemplate, "text/html; charset=UTF-8", bodyLength, body);
    }
    else
    {
        const char* body = "404 - Not Found";
        int bodyLength = strlen(body);
        snprintf(response, BUFFER_SIZE, not_found_response, bodyLength, body);
    }
}

void handleTraceRequest(SocketData* socket, char* response, const char* responseTemplate, const char* notFoundResponse)
{
    const char* body;
    const char* contentType;
    char* startUrl = strchr(socket->buffer, ' ') + 1;
    char* copyUrl = (char*)malloc(sizeof(char) * strlen(startUrl) + 1);
    strcpy(copyUrl, startUrl);
    copyUrl[strlen(copyUrl)] = '\0';
    char* urlEnd = strchr(copyUrl, ' ');
    *urlEnd = '\0';
    if (strcmp(copyUrl, "/") == 0)
    {
        body = socket->buffer;
        contentType = "message/http";
        int requestLength = (int)strlen(socket->buffer);
        snprintf(response, BUFFER_SIZE, responseTemplate, contentType, requestLength, body);
    }
    else
    {
        body = "404 - Not Found";
        int bodyLength = strlen(body);
        snprintf(response, BUFFER_SIZE, notFoundResponse, bodyLength, body);
    }
}

void handleRequest(SocketData* socket) {
    const char* responseTemplate =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n%s";

    const char* headersResponseTemplate =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n";

    const char* notImplementedResponse =
        "HTTP/1.1 501 Not Implemented\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n%s";

    const char* notFoundResponse =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n%s";
    const char* notFoundResponseHeaders =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n";

    char response[BUFFER_SIZE];
    const char* body;
    const char* contentType = "text/html; charset=UTF-8";

    if (strncmp(socket->buffer, "GET", 3) == 0) {
        body = handleGetRequest(socket);
        int bodyLen = strlen(body);
        if (strcmp(body, "404 - Not Found") == 0) {
            snprintf(response, BUFFER_SIZE, notFoundResponse, bodyLen, body);
        }
        else
        {
            snprintf(response, BUFFER_SIZE, responseTemplate, contentType, bodyLen, body);
        }
    }
    else if (strncmp(socket->buffer, "POST", 4) == 0) {
        handlePostRequest(socket, response, responseTemplate, notFoundResponse);
    }
    else if (strncmp(socket->buffer, "HEAD", 4) == 0) {
        body = handleGetRequest(socket);
        int contentLen = (int)strlen(body);
        if (strcmp(body, "404 - Not Found") == 0)
        {
            snprintf(response, BUFFER_SIZE, notFoundResponseHeaders, 0);
        }
        else
        {
            snprintf(response, BUFFER_SIZE, headersResponseTemplate, contentType, contentLen);
        }
    }
    else if (strncmp(socket->buffer, "OPTIONS", 7) == 0) {
        handleOptionsRequest(socket, response);
        return;
    }
    else if (strncmp(socket->buffer, "PUT", 3) == 0) {
        bool isSuccessful;
        handlePutRequest(socket, responseTemplate, response, &isSuccessful);
        if (!isSuccessful)
        {
            body = "404 - Not Found";
            int bodyLength = strlen(body);
            snprintf(response, BUFFER_SIZE, notFoundResponse, bodyLength, body);
        }
    }
    else if (strncmp(socket->buffer, "DELETE", 6) == 0) {
        bool isSuccessful;
        handleDeleteRequest(socket, responseTemplate, response, &isSuccessful);
        if (!isSuccessful)
        {
            body = "404 - Not Found";
            int bodyLength = strlen(body);
            snprintf(response, BUFFER_SIZE, notFoundResponse, bodyLength, body);
        }
    }
    else if (strncmp(socket->buffer, "TRACE", 5) == 0) {
        handleTraceRequest(socket, response, responseTemplate, notFoundResponse);
    }
    else {
        body = "501 Not Implemented";
        snprintf(response, BUFFER_SIZE, notImplementedResponse, (int)strlen(body), body);
        strcpy(socket->buffer, response);
        socket->len = (int)strlen(response);
        socket->send = SEND;
        return;
    }

    strcpy(socket->buffer, response);
    socket->len = (int)strlen(response);
    socket->send = SEND;
}


void sendMessage(int index) {
    SOCKET socketId = sockets[index].id;

    int bytesSent = send(socketId, sockets[index].buffer, sockets[index].len, 0);
    if (SOCKET_ERROR == bytesSent) {
        cout << "HTTP Server: Error at send(): " << WSAGetLastError() << endl;
        return;
    }

    closesocket(socketId);
    removeSocket(index);
}

void updateSocketsTime(SocketData* sockets, int& socketsNum)
{
    time_t timeNow;
    for (int i = 1; i < MAX_SOCKETS; i++) {
        timeNow = time(0);
        if ((timeNow - sockets[i].timeOut > 120) && (sockets[i].timeOut != 0) && sockets[i].recv != EMPTY && sockets[i].send != EMPTY) {
            cout << "Removing socket due to timeout: " << sockets[i].id << endl;
            //closesocket(sockets[i].id);
            removeSocket(i);
        }
    }
}