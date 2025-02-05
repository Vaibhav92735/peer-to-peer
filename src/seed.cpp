#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>

#define PORT 5000  // Change as needed

std::unordered_map<std::string, int> peer_list; // Stores peer (IP:Port)
std::mutex pl_mutex; // Mutex for thread safety

void handle_peer(int client_socket) {
    char buffer[1024] = {0};
    read(client_socket, buffer, 1024);
    std::string request(buffer);
    std::istringstream ss(request);
    std::string command, ip;
    int port;
    ss >> command >> ip >> port;

    std::lock_guard<std::mutex> lock(pl_mutex);

    if (command == "REGISTER") {
        peer_list[ip + ":" + std::to_string(port)] = port;
        std::cout << "Registered Peer: " << ip << ":" << port << std::endl;
        std::string response = "OK";
        send(client_socket, response.c_str(), response.size(), 0);
    } 
    else if (command == "DEAD_NODE") {
        peer_list.erase(ip + ":" + std::to_string(port));
        std::cout << "Removed Dead Peer: " << ip << ":" << port << std::endl;
    }
    
    close(client_socket);
}

void start_seed() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 5);
    std::cout << "Seed Node listening on port " << PORT << std::endl;

    while (true) {
        new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        std::thread(handle_peer, new_socket).detach();
    }
}

int main() {
    start_seed();
    return 0;
}
