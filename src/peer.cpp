#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <queue>
#include <thread>
#include <mutex>
#include <chrono>
#include <random>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <bits/stdc++.h>

using namespace std;

// Global variables
const int no_of_threads = 3;
std::queue<int> job_queue;
std::mutex mtx;
std::mutex peers_mutex; // Mutex for peers_connected
const std::string MY_IP = "127.0.0.1";
int PORT;
std::unordered_set<std::string> seeds_addr;
std::unordered_set<std::string> peer_set_from_seed;
std::vector<class Peer> peers_connected;
std::vector<std::string> MessageList;
std::vector<std::string> connect_seed_addr;
int sock;

// Class for Peer objects
class Peer
{
public:
    int i = 0;
    std::string address;
    Peer(std::string addr) : address(addr) {}

    // Define equality operator
    bool operator==(const Peer &other) const
    {
        return address == other.address;
    }
};

// Function to write outputs to a file
void write_output_to_file(const std::string &output)
{
    std::ofstream file("outputpeer.txt", std::ios::app);
    if (file.is_open())
    {
        file << output << "\n";
        file.close();
    }
    else
    {
        std::cerr << "Write Failed\n";
    }
}

// Function to get the current timestamp
std::string timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto epoch = now_ms.time_since_epoch();
    auto value = std::chrono::duration_cast<std::chrono::milliseconds>(epoch);
    return std::to_string(value.count());
}

// Function to read seed addresses from config file
void read_addr_of_seeds()
{
    std::ifstream file("config.txt");
    if (file.is_open())
    {
        std::string line;
        while (std::getline(file, line))
        {
            seeds_addr.insert(line);
        }
        file.close();
    }
    else
    {
        std::cerr << "Read from config failed\n";
    }
}

// Function to calculate the total number of available seeds
int total_available_seeds()
{
    return seeds_addr.size();
}

// Function to generate k random numbers in a given range
std::unordered_set<int> generate_k_random_numbers_in_range(int lower, int higher, int k)
{
    std::unordered_set<int> random_numbers_set;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(lower, higher);
    while (random_numbers_set.size() < k)
    {
        random_numbers_set.insert(dist(gen));
    }
    return random_numbers_set;
}

// Function to create a socket
void create_socket()
{
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        std::cerr << "Socket Creation Error: " << strerror(errno) << "\n";
    }
}

// Function to bind the socket
void bind_socket()
{
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (bind(sock, (sockaddr *)&address, sizeof(address)) < 0)
    {
        std::cerr << "Socket Binding Error: " << strerror(errno) << "\n";
        bind_socket();
    }
}

// Function to forward gossip messages
void forward_gossip_message(const std::string &received_message)
{
    std::string hash = received_message;
    if (std::find(MessageList.begin(), MessageList.end(), hash) == MessageList.end())
    {
        MessageList.push_back(hash);
        std::cout << "Received: " << received_message << "\n";
        write_output_to_file(received_message);
        for (const auto &peer : peers_connected)
        {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            size_t colon_pos = peer.address.find(':');
            std::string ip = peer.address.substr(0, colon_pos);
            std::string port_str = peer.address.substr(colon_pos + 1);
            if (port_str.empty())
            {
                std::cerr << "Invalid port in peer address: " << peer.address << "\n";
                continue;
            }
            int port = std::stoi(port_str);
            sockaddr_in address;
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &address.sin_addr);
            if (connect(sock, (sockaddr *)&address, sizeof(address)) == 0)
            {
                send(sock, received_message.c_str(), received_message.size(), 0);
                close(sock);
            }
            // else
            // {
            //     std::cerr << "Peer Down " << peer.address << ": " << strerror(errno) << "\n";
            // }
        }
    }
}

// Function to handle peer connections
void handle_peer(int conn, sockaddr_in addr)
{
    char buffer[1024];
    while (true)
    {
        int bytes_received = recv(conn, buffer, sizeof(buffer), 0);
        if (bytes_received > 0)
        {
            std::string message(buffer, bytes_received);
            std::string received_data = message;
            if (message.find("New Connect Request From") != std::string::npos)
            {
                if (peers_connected.size() < 4)
                {
                    send(conn, "New Connect Accepted", 20, 0);
                    std::string peer_address = std::string(inet_ntoa(addr.sin_addr)) + ":" + message.substr(message.find_last_of(':') + 1);
                    {
                        std::lock_guard<std::mutex> lock(peers_mutex);
                        peers_connected.emplace_back(Peer(peer_address));
                    }
                }
            }
            else if (message.find("Ping Request") != std::string::npos)
            {
                std::string ping_reply = "Ping Reply:" + message.substr(message.find(':') + 1) + ":" + MY_IP;
                send(conn, ping_reply.c_str(), ping_reply.size(), 0);
            }
            else if (message.find("GOSSIP") != std::string::npos)
            {
                forward_gossip_message(received_data);
            }
        }
        else
        {
            break;
        }
    }
    close(conn);
}

// Function to begin listening for connections
void begin()
{
    listen(sock, 5);
    std::cout << "Peer is Listening\n";
    while (true)
    {
        sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        int conn = accept(sock, (sockaddr *)&addr, &addr_len);
        if (conn < 0)
        {
            std::cerr << "Accept Error: " << strerror(errno) << "\n";
            continue;
        }
        std::thread(handle_peer, conn, addr).detach();
    }
}

// Function to connect to peers
void connect_peers(const std::vector<std::string> &complete_peer_list, const std::unordered_set<int> &selected_peer_nodes_index)
{
    for (int i : selected_peer_nodes_index)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        std::string peer_addr = complete_peer_list[i];
        size_t colon_pos = peer_addr.find(':');
        std::string ip = peer_addr.substr(0, colon_pos);
        std::string port_str = peer_addr.substr(colon_pos + 1);
        if (port_str.empty())
        {
            std::cerr << "Invalid port in peer address: " << peer_addr << "\n";
            continue;
        }
        if(port_str.size() < 4) {
            std::cerr << "Invalid port in peer address: " << peer_addr << "\n";
            continue;
        }
        cout << "The Port is: " << port_str << "\n";
        int port = std::stoi(port_str);
        if (port == PORT)
        {
            cout << "Peer is trying to connect to itself\n";
            continue;
        }
        sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &address.sin_addr);
        if (connect(sock, (sockaddr *)&address, sizeof(address)) == 0)
        {
            {
                std::lock_guard<std::mutex> lock(peers_mutex);
                peers_connected.emplace_back(Peer(peer_addr));
            }
            std::string message = "New Connect Request From:" + MY_IP + ":" + std::to_string(PORT);
            send(sock, message.c_str(), message.size(), 0);
            char buffer[1024] = {0};                                        // Initialize buffer to zero
            int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0); // Leave space for null terminator
            if (bytes_received > 0)
            {
                buffer[bytes_received] = '\0'; // Null-terminate the buffer
                // std::cout << buffer << "\n";
            }
            else
            {
                std::cerr << "Receive failed: " << strerror(errno) << "\n";
            }
            close(sock);
        }
        else
        {
            std::cerr << "Peer Connection Error: " << strerror(errno) << port << "\n";
        }
    }
}

// Function to join k peers
void join_atmost_k_peers(const std::vector<std::string> &complete_peer_list)
{
    if (!complete_peer_list.empty())
    {
        // int tr = std::min(static_cast<int>(complete_peer_list.size()), 4);
        int tr = static_cast<int>(complete_peer_list.size());
        int limit = rand() % tr + 1;
        auto selected_peer_nodes_index = generate_k_random_numbers_in_range(0, complete_peer_list.size() - 1, limit);
        connect_peers(complete_peer_list, selected_peer_nodes_index);
    }
}

// Function to union peer lists
std::vector<std::string> union_peer_lists(const std::string &complete_peer_list)
{
    std::vector<std::string> peers;
    size_t pos = 0;
    std::string temp = complete_peer_list;
    while ((pos = temp.find(',')) != std::string::npos)
    {
        peers.push_back(temp.substr(0, pos));
        temp.erase(0, pos + 1);
    }
    // MY_IP = peers.back().substr(0, peers.back().find(':'));
    for (const auto &peer : peers)
    {
        if (!peer.empty())
        {
            peer_set_from_seed.insert(peer);
        }
    }
    return std::vector<std::string>(peer_set_from_seed.begin(), peer_set_from_seed.end());
}

// Function to connect to seeds
void connect_seeds()
{
    for (const auto &seed : connect_seed_addr)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        size_t colon_pos = seed.find(':');
        std::string ip = seed.substr(0, colon_pos);
        std::string port_str = seed.substr(colon_pos + 1);
        if (port_str.empty())
        {
            std::cerr << "Invalid port in seed address: " << seed << "\n";
            continue;
        }
        int port = std::stoi(port_str);
        sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &address.sin_addr);
        if (connect(sock, (sockaddr *)&address, sizeof(address)) == 0)
        {
            std::string MY_ADDRESS = MY_IP + ":" + std::to_string(PORT);
            send(sock, MY_ADDRESS.c_str(), MY_ADDRESS.size(), 0);
            char buffer[10240];
            recv(sock, buffer, sizeof(buffer), 0);
            std::string message(buffer);
            auto complete_peer_list = union_peer_lists(message);
            close(sock);
        }
        else
        {
            std::cerr << "Seed Connection Error: " << strerror(errno) << "\n";
        }
    }
    join_atmost_k_peers(std::vector<std::string>(peer_set_from_seed.begin(), peer_set_from_seed.end()));
}

// Function to register with k seeds
void register_with_k_seeds()
{
    int n = seeds_addr.size();
    auto seed_nodes_index = generate_k_random_numbers_in_range(0, n - 1, n / 2 + 1);
    for (int i : seed_nodes_index)
    {
        connect_seed_addr.push_back(*std::next(seeds_addr.begin(), i));
    }
    connect_seeds();
}

// Function to report a dead peer
void report_dead(const std::string &peer)
{
    std::lock_guard<std::mutex> lock(peers_mutex);
    auto it = std::find_if(peers_connected.begin(), peers_connected.end(), [&](const Peer &p)
                           { return p.address == peer; });
    if (it != peers_connected.end())
    {
        std::string dead_message = "Dead Node:" + peer + ":" + timestamp() + ":" + MY_IP;
        std::cout << dead_message << "\n";
        write_output_to_file(dead_message);

        // Send the dead message to all connected seeds
        for (const auto &seed : seeds_addr)
        {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            size_t colon_pos = seed.find(':');
            std::string ip = seed.substr(0, colon_pos);
            std::string port_str = seed.substr(colon_pos + 1);
            if (port_str.empty())
            {
                std::cerr << "Invalid port in seed address: " << seed << "\n";
                continue;
            }
            int port = std::stoi(port_str);
            sockaddr_in address;
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &address.sin_addr);
            if (connect(sock, (sockaddr *)&address, sizeof(address)) == 0)
            {
                send(sock, dead_message.c_str(), dead_message.size(), 0);
                close(sock);
            }
            else
            {
                std::cerr << "Seed Connection Error: " << strerror(errno) << "\n";
            }
        }

        peers_connected.erase(it);
    }
}

// Function for pinging
void pinging()
{
    while (true)
    {
        // cout << "My IP is: " << MY_IP << "\n";
        std::string ping_request = "Ping Request:" + timestamp() + ":" + MY_IP;
        std::cout << ping_request << "\n";
        for (auto &peer : peers_connected)
        {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            size_t colon_pos = peer.address.find(':');
            std::string ip = peer.address.substr(0, colon_pos);
            std::string port_str = peer.address.substr(colon_pos + 1);
            if (port_str.empty())
            {
                std::cerr << "Invalid port in peer address: " << peer.address << "\n";
                continue;
            }
            int port = std::stoi(port_str);
            sockaddr_in address;
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &address.sin_addr);
            if (connect(sock, (sockaddr *)&address, sizeof(address)) == 0)
            {
                send(sock, ping_request.c_str(), ping_request.size(), 0);
                char buffer[1024];
                recv(sock, buffer, sizeof(buffer), 0);
                std::cout << buffer << "\n";
                close(sock);
                peer.i = 0;
            }
            else
            {
                peer.i++;
                if (peer.i == 3)
                {
                    report_dead(peer.address);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(13));
    }
}

// Function to generate and send gossip messages
void generate_send_gossip_message(int i)
{
    std::string gossip_message = timestamp() + ":" + MY_IP + ":" + std::to_string(PORT) + ":" + " GOSSIP " + std::to_string(i + 1);
    MessageList.push_back(gossip_message);
    for (const auto &peer : peers_connected)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        size_t colon_pos = peer.address.find(':');
        std::string ip = peer.address.substr(0, colon_pos);
        std::string port_str = peer.address.substr(colon_pos + 1);
        if (port_str.empty())
        {
            std::cerr << "Invalid port in peer address: " << peer.address << "\n";
            continue;
        }
        int port = std::stoi(port_str);
        sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &address.sin_addr);
        if (connect(sock, (sockaddr *)&address, sizeof(address)) == 0)
        {
            send(sock, gossip_message.c_str(), gossip_message.size(), 0);
            close(sock);
        }
    }
}

// Function to gossip messages
void gossip()
{
    for (int i = 0; i < 10; i++)
    {
        generate_send_gossip_message(i);
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

// Function to create worker threads
void create_workers()
{
    for (int i = 0; i < no_of_threads; i++)
    {
        std::thread([]()
                    {
            while (true) {
                int x;
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    if (!job_queue.empty()) {
                        x = job_queue.front();
                        job_queue.pop();
                    } else {
                        continue;
                    }
                }
                if (x == 1) {
                    create_socket();
                    bind_socket();
                    begin();
                } else if (x == 2) {
                    pinging();
                } else if (x == 3) {
                    gossip();
                }
            } })
            .detach();
    }
}

// Function to create jobs in the queue
void create_jobs()
{
    for (int i = 1; i <= 3; i++)
    {
        job_queue.push(i);
    }
}

int main()
{
    std::cout << "Enter port: ";
    std::cin >> PORT;

    read_addr_of_seeds();
    int n = total_available_seeds();
    register_with_k_seeds();

    create_workers();
    create_jobs();

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1)); // Sleep to avoid busy-waiting
    }

    return 0;
}
