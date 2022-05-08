#include <zmq.hpp>
#include <string>
#include <iostream>
#include <pthread.h>
#include "glremote_server/glremote_server.h"

int main(int argc, char* argv[]) {
    bool enableStreaming = false;

    if (argc > 3){
        if (strcmp(argv[3], "true") == 0){
            enableStreaming = true;
            
        }else{
            enableStreaming = false;
        }
    }
    Server server(argv[1], argv[2], enableStreaming, argv[4]);
    server.server_bind();
    
    std::thread main_loop{&Server::run, &server};
    
    main_loop.join();
    return 0;
}
