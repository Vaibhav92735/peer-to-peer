#include <iostream>
#include <vector>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <random>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>

#define NUM_MESSAGES 10
#define GOSSIP_INTERVAL 5  // Gossip every 5 sec
#define PING_INTERVAL 13   // Ping every 13 sec
#define SELF_PORT 6000     // Change for multiple peers

std::vector<std::string> seeds;
std::unordered_set<std::string> peers;
std::unordered_set<std::string> message_list;  // To prevent duplicate messages

void load_seeds() {
    std::ifstream file("config.json");
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("ip") != std::string::npos) {
            std::string ip = line.substr(line.find(":") + 3, line.find(",") - line.find(":") - 4);
            std::getline(file, line);
            int port = std::stoi(line.substr(line.find(":") + 2));
            seeds.push_back(ip + ":" + std::to_string(port));
        }
    }
}

void send_message(const std::string& ip, int port, const std::string& message) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) >= 0) {
        send(sock, message.c_str(), message.size(), 0);
    }
    close(sock);
}

void register_with_seeds() {
    for (const auto& seed : seeds) {
        std::string ip = seed.substr(0, seed.find(":"));
        int port = std::stoi(seed.substr(seed.find(":") + 1));

        std::string message = "REGISTER 127.0.0.1 " + std::to_string(SELF_PORT);
        send_message(ip, port, message);
    }
}

void gossip() {
    for (int i = 0; i < NUM_MESSAGES; ++i) {
        std::string msg = "MSG: " + std::to_string(time(0)) + ": 127.0.0.1";
        message_list.insert(msg);

        for (const auto& peer : peers) {
            std::string ip = peer.substr(0, peer.find(":"));
            int port = std::stoi(peer.substr(peer.find(":") + 1));
            send_message(ip, port, msg);
        }
        std::this_thread::sleep_for(std::chrono::seconds(GOSSIP_INTERVAL));
    }
}

void check_liveness() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(PING_INTERVAL));
        std::vector<std::string> dead_nodes;

        for (const auto& peer : peers) {
            std::string ip = peer.substr(0, peer.find(":"));
            int port = std::stoi(peer.substr(peer.find(":") + 1));

            if (system(("ping -c 1 " + ip + " > /dev/null 2>&1").c_str()) != 0) {
                dead_nodes.push_back(peer);
            }
        }

        for (const auto& dead : dead_nodes) {
            peers.erase(dead);
            for (const auto& seed : seeds) {
                std::string seed_ip = seed.substr(0, seed.find(":"));
                int seed_port = std::stoi(seed.substr(seed.find(":") + 1));
                send_message(seed_ip, seed_port, "DEAD_NODE " + dead);
            }
        }
    }
}

void listen_for_messages() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SELF_PORT);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 5);
    
    while (true) {
        new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        char buffer[1024] = {0};
        read(new_socket, buffer, 1024);
        std::string msg(buffer);

        if (message_list.find(msg) == message_list.end()) {
            message_list.insert(msg);
            std::cout << "Received: " << msg << std::endl;
            for (const auto& peer : peers) {
                send_message(peer.substr(0, peer.find(":")), std::stoi(peer.substr(peer.find(":") + 1)), msg);
            }
        }
        close(new_socket);
    }
}

int main() {
    load_seeds();
    register_with_seeds();

    std::thread(listen_for_messages).detach();
    std::thread(gossip).detach();
    std::thread(check_liveness).detach();

    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
