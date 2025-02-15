#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <bits/stdc++.h>

using namespace std;

#define MY_IP "0.0.0.0"
#define BUFFER_SIZE 1024

std::vector<std::string> peer_list;
std::mutex peer_list_mutex;
int PORT;

// Write the outputs to the file
void write_output_to_file(const std::string& output) {
    std::ofstream file("outputseed.txt", std::ios::app);
    if (file.is_open()) {
        file << output << std::endl;
        file.close();
    } else {
        std::cerr << "Write Failed" << std::endl;
    }
}

// Convert list of connected peers to a comma-separated string
std::string list_to_string(const std::vector<std::string>& peer_list) {
    std::string peerListStr = ",";
    for (const auto& peer : peer_list) {
        peerListStr += peer + ",";
    }
    return peerListStr;
}

// Remove a dead node from the peer list
void remove_dead_node(const std::string& message) {
    // Log the received dead node message
    std::cout << "Received Dead Node Message: " << message << std::endl;
    write_output_to_file("Received Dead Node Message: " + message);

    // Extract the dead node's address from the message
    size_t colon1 = message.find(':'); // First colon after "Dead Node"
    size_t colon2 = message.find(':', colon1 + 1); // Second colon after IP
    size_t colon3 = message.find(':', colon2 + 1); // Third colon after port

    if (colon1 == std::string::npos || colon2 == std::string::npos || colon3 == std::string::npos) {
        std::cerr << "Invalid dead node message format: " << message << std::endl;
        return;
    }

    // Extract IP and port (between the first and third colons)
    std::string dead_node = message.substr(colon1 + 1, colon3 - (colon1 + 1));
    std::cout << "Dead Node Identified: " << dead_node << std::endl;

    // Lock the peer list to ensure thread safety
    std::lock_guard<std::mutex> lock(peer_list_mutex);

    // Find and remove the dead node from the peer list
    auto it = std::find(peer_list.begin(), peer_list.end(), dead_node);
    if (it != peer_list.end()) {
        std::cout << "Removing Dead Node: " << *it << std::endl;
        peer_list.erase(it);
    } else {
        std::cout << "Dead Node Not Found in Peer List: " << dead_node << std::endl;
    }

    // Log the updated peer list
    std::cout << "Updated Peer List:" << std::endl;
    for (const auto& peer : peer_list) {
        std::cout << peer << std::endl;
    }
}

// Handle communication with a peer
void handle_peer(int conn, const std::string& addr) {
    char buffer[BUFFER_SIZE];
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_received = recv(conn, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) {
            break;
        }

        std::string message(buffer, bytes_received);
        if (message.substr(0, 9) == "Dead Node") {
            remove_dead_node(message);
        } else {
            size_t colon = message.find(':');
            std::string peer_addr = addr + ":" + message.substr(colon + 1);

            std::lock_guard<std::mutex> lock(peer_list_mutex);
            peer_list.push_back(peer_addr);

            std::string output = "Received Connection from " + peer_addr;
            std::cout << output << std::endl;
            write_output_to_file(output);

            std::string peerListStr = list_to_string(peer_list);
            send(conn, peerListStr.c_str(), peerListStr.size(), 0);
        }
    }
    close(conn);
}

// Begin listening for incoming connections
void begin(int sockfd) {
    if (listen(sockfd, 5) < 0) {
        std::cerr << "Listen Failed" << std::endl;
        return;
    }

    std::cout << "Seed is Listening" << std::endl;

    while (true) {
        struct sockaddr_in peer_addr;
        socklen_t addr_len = sizeof(peer_addr);
        int conn = accept(sockfd, (struct sockaddr*)&peer_addr, &addr_len);
        if (conn < 0) {
            std::cerr << "Accept Failed" << std::endl;
            continue;
        }

        std::string peer_ip = inet_ntoa(peer_addr.sin_addr);
        std::thread(handle_peer, conn, peer_ip).detach();
    }
}

int main() {
    std::cout << "Enter Port No. for listening: ";
    std::cin >> PORT;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket Creation Error" << std::endl;
        return 1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr(MY_IP);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Socket Binding Error" << std::endl;
        close(sockfd);
        return 1;
    }

    begin(sockfd);

    close(sockfd);
    return 0;
}
