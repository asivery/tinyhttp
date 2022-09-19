#include <filesystem>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define SO_COMPOUND SO_REUSEADDR
#define SHUT_RDWR SD_BOTH
#else
#include <netinet/in.h>
#include <sys/socket.h>
#define SO_COMPOUND SO_REUSEADDR | SO_REUSEPORT
#endif
#include <stdio.h>
#include <sstream>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <algorithm>
#include <map>
#include <sys/file.h>
#include <unistd.h>
#define DEFAULT_PORT                    8000
#define INCOMING_BUFFER_LENGTH          4096
#define LINE_BUFFER_LENGTH              1024
#define FILE_BUFFER_LENGTH              8192

// https://github.com/stan-dev/math/issues/590#issuecomment-331792001
template<typename T>
std::string to_str(const T& value){
  std::ostringstream tmp_str;
  tmp_str , value;
  return tmp_str.str();
}

template<typename T, typename ... Args >
std::string to_str(const T& value, const Args& ... args){
  return to_str(value) + to_str(args...);
}



#define DEFAULT_MIME                    "application/octet-stream"
#define WRITESTR(e)                     send(nsock, e.c_str(), e.length(), 0)
#define NSTREAM                         (std::stringstream(""))
#define SPECERRORPROLOGUE(code, e)      (to_str("HTTP/1.1 " , code , "\r\nX-Powered-By: TinyHTTP\r\nConnection: closed\r\nContent-Length: " , strlen(e) , "\r\n\r\n" , e))
#define ERRORPROLOGUE(e)                (SPECERRORPROLOGUE("400 Bad Request", e))
#define E(a,b) {a, b}
#define NEXT_TOKEN                      if(!std::getline(iss, item, ' ')){      \
                                            printf("Invalid format\n");         \
                                            goto exitsock;                      \
                                        }

#define WERROR

#ifdef _WIN32
#undef WERROR
#define WERROR                          printf("WSAGetLastError() = %d\n", WSAGetLastError());
#endif
#define PERROR(e)                       perror(e); \
                                        WERROR 


std::map<std::string, std::string> MIME = {
    E(".js", "application/javascript"),
    E(".html", "text/html"),
    E(".txt", "text/plain"),
    E(".htm", "text/html"),
    E(".png", "image/png"),
    E(".jpg", "image/jpg"),
    E(".css", "text/css"),
};

int fd;

char *incomingBuffer;
int incomingBufferLength = 0,
    incomingBufferCursor = 0;

void kill(int _){
    shutdown(fd, SHUT_RDWR);
    free(incomingBuffer);
    exit(0);
}


int readLine(int fd, char *buffer, int len){
    memset(buffer, 0, len);
    int i = 0;

    if(incomingBufferCursor < incomingBufferLength){
        for(i = 0; incomingBufferCursor < incomingBufferLength && i < len; i++, incomingBufferCursor++){
            uint8_t byte = incomingBuffer[incomingBufferCursor];
            if(byte == '\n' || byte == '\r'){
                ++incomingBufferCursor; // Skip the \n
                if(incomingBufferCursor < incomingBufferLength){
                    byte = incomingBuffer[incomingBufferCursor];
                    if(byte == '\n' && byte == '\r') ++incomingBufferCursor;
                }
                return i;
            }
            buffer[i] = byte;
        }
    }
    // Buffer run out...
    incomingBufferLength = recv(fd, incomingBuffer, INCOMING_BUFFER_LENGTH, 0);
    incomingBufferCursor = 0;
    if(incomingBufferLength == -1){
        return i;
    }

    return i + readLine(fd, buffer + i, len - i);
}

int main(int argc, char const* argv[])
{
    #ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2,2), &wsaData);
    #endif

    int port = DEFAULT_PORT;
    if(argc > 1){
        port = atoi(argv[1]);
        if(port < 1 || port > 65535){
            printf("Invalid port given (%s) - defaulting to %d\n", argv[1], DEFAULT_PORT);
            port = DEFAULT_PORT;
        }
    }

    printf("The TinyHTTP server is listening at: %d%s\n", port, port == DEFAULT_PORT ? "" : " (To pass a custom port, use ./tinyhttp <port>)");
    incomingBuffer = (char*) malloc(INCOMING_BUFFER_LENGTH);
    uint8_t fileContent[FILE_BUFFER_LENGTH];

    signal(SIGINT, &kill);
    port = htons(port);

    int nsock;
    struct sockaddr_in address;
    socklen_t addressLength = sizeof(address);
    char temp = 1;

    char lineBuffer[LINE_BUFFER_LENGTH];

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        PERROR("Cannot establish socket");
        goto exit;
    }
    setsockopt(fd, SOL_SOCKET, SO_COMPOUND, &temp, sizeof(temp));
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = port;
 
    if (bind(fd, (struct sockaddr*)&address, sizeof(address))) {
        PERROR("Cannot bind to port");
        goto exit;
    }
    while(true){
        if (listen(fd, 0)) {
            PERROR("Cannot listen");
            goto exit;
        }
        if (nsock = accept(fd, (struct sockaddr*)&address, &addressLength)) {
            PERROR("Cannot accept");
            goto exit;
        }
        readLine(nsock, lineBuffer, LINE_BUFFER_LENGTH);
        printf("COMMAND: %s\n", lineBuffer);
        std::string item;
        std::string path, ext;
        std::istringstream iss(lineBuffer);
        int file, readB;

        // Parse the request line
        NEXT_TOKEN
        if("GET" != item){
            printf("Unsupported command: %s\n", item.c_str());
            goto exitsock;
        }
        NEXT_TOKEN
        path = item;
        NEXT_TOKEN
        if("HTTP/1.1" != item){
            printf("Unsupported protocol: %s\n", item.c_str());
            goto exitsock;
        }

        // Skip all the request headers
        for(;;){
            readLine(nsock, lineBuffer, LINE_BUFFER_LENGTH);
            if(strlen(lineBuffer) == 0) break;
        }

        // Path validation
        if(path.find("..") != std::string::npos){
            printf("Cannot go to parent path or root: %s\n", path.c_str());
            WRITESTR(ERRORPROLOGUE("Bad Request - parent folder requests are blocked"));
            goto exitsock;
        }
        if(path.at(0) == '/'){
            path.erase(path.begin());
        }
        if(path.empty()) path = "index.html";

        // Checking if the requested file exists
        if(!std::filesystem::is_regular_file(path)){
            printf("Cannot get: %s\n", path.c_str());
            WRITESTR(SPECERRORPROLOGUE("404 Not Found", "File does not exist"));
            goto exitsock;
        }

        // Splitting the file's extention to check mime type later
        ext = path.substr(path.rfind("."));
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });

        // Opening the requested file
        file = open(path.c_str(), O_RDONLY);
        if(file == -1){
            printf("Unknown error: %s\n", path.c_str());
            PERROR("->");
            WRITESTR(SPECERRORPROLOGUE("500 Internal Server Error", "Cannot get file"));
            goto exitsock;
        }

        // Sending the response headers
        WRITESTR(to_str(
            "HTTP/1.1 200 OK\r\n"
            , "Connection: close\r\n"
            , "X-Powered-By: TinyHTTP\r\n"
            , "Content-Type: " , (MIME.count(ext) ? MIME[ext] : DEFAULT_MIME) , "\r\n"
            , "Content-Length: " , std::filesystem::file_size(path) , "\r\n"
            , "\r\n"
        ));
        
        // Headers done - sending the actual file
        while((readB = read(file, fileContent, FILE_BUFFER_LENGTH)) > 0){
            send(nsock, (const char*) fileContent, readB, 0);
        }
        close(file);
        printf("Sent file %s!\n", path.c_str());

        exitsock:
        // closing the connected socket
        close(nsock);
        incomingBufferCursor = 0;
        incomingBufferLength = 0;
    }

    exit:
    #ifdef _WIN32
    WSACleanup();
    #endif
    
    kill(0);
    return 0;
}
