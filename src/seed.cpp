#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <jsoncpp/json/json.h>  // Install: sudo apt install libjsoncpp-dev

#define DEFAULT_SEEDS 10
#define BASE_PORT 5000

std::unordered_map<std::string, int> peer_list;
std::mutex pl_mutex;

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

void start_seed(int port) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Error binding to port " << port << std::endl;
        return;
    }

    listen(server_fd, 5);
    std::cout << "Seed Node listening on port " << port << std::endl;

    while (true) {
        new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        std::thread(handle_peer, new_socket).detach();
    }
}

void save_config(const std::vector<int>& ports) {
    Json::Value root;
    for (int port : ports) {
        Json::Value seed;
        seed["ip"] = "127.0.0.1";  // Assuming localhost
        seed["port"] = port;
        root["seeds"].append(seed);
    }

    std::ofstream file("../config.json");
    if (!file) {
        std::cerr << "Error opening config file!" << std::endl;
        return;
    }
    
    file << root.toStyledString();
    file.close();
    std::cout << "Config file saved: ../config.json" << std::endl;
}

int main() {
    int n;
    std::cout << "Enter the number of seed nodes (default " << DEFAULT_SEEDS << "): ";
    std::string input;
    std::getline(std::cin, input);
    
    n = (input.empty()) ? DEFAULT_SEEDS : std::stoi(input);
    
    std::vector<int> seed_ports;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < n; i++) {
        int port = BASE_PORT + i;
        seed_ports.push_back(port);
        threads.emplace_back(start_seed, port);
    }

    save_config(seed_ports);

    for (auto& thread : threads) {
        thread.join();
    }

    return 0;
}
