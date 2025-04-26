#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <ctime>
#include <arpa/inet.h>
#include <unistd.h>
#include <mutex>
#include <sstream>
#include <iomanip>

#define MULTICAST_GROUP "224.1.1.1"
#define MULTICAST_PORT 8889
#define SERVER_PORT 8888
#define DISCOVERY_INTERVAL 10

std::unordered_map<std::string, std::pair<time_t, std::string>> clients;
std::mutex clients_mutex;

std::string get_current_time() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
    return ss.str();
}

void send_discovery(int sock) {
    sockaddr_in mcast_addr{};
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
    mcast_addr.sin_port = htons(MULTICAST_PORT);

    while (true) {
        std::string msg = "DISCOVERY_SERVER=" + get_current_time();
        sendto(sock, msg.c_str(), msg.size(), 0, 
              (sockaddr*)&mcast_addr, sizeof(mcast_addr));
        
        std::cout << "[Discovery] Sent multicast request\n";
        std::this_thread::sleep_for(std::chrono::seconds(DISCOVERY_INTERVAL));
    }
}

void listen_responses(int sock) {
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    char buf[1024];

    while (true) {
        int n = recvfrom(sock, buf, sizeof(buf), 0, 
                        (sockaddr*)&client_addr, &len);
        if (n <= 0) continue;
        
        buf[n] = '\0';
        std::string response(buf);
        std::string client_ip = inet_ntoa(client_addr.sin_addr);

        if (response.find("CLIENT_ID=") != std::string::npos) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients[client_ip] = {time(nullptr), response};
            std::cout << "[Client] " << client_ip << " responded: " 
                      << response << "\n";
        }
    }
}

void check_inactive_clients() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::lock_guard<std::mutex> lock(clients_mutex);
        
        time_t now = time(nullptr);
        for (auto it = clients.begin(); it != clients.end(); ) {
            if (now - it->second.first > 20) {
                std::cout << "[Timeout] Client " << it->first << " removed\n";
                it = clients.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void request_system_info(int sock) {
    sockaddr_in client_addr{};
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto& [ip, data] : clients) {
            client_addr.sin_family = AF_INET;
            client_addr.sin_addr.s_addr = inet_addr(ip.c_str());
            client_addr.sin_port = htons(SERVER_PORT);
            
            std::string msg = "GET_SYSTEM_INFO";
            sendto(sock, msg.c_str(), msg.size(), 0,
                  (sockaddr*)&client_addr, sizeof(client_addr));
        }
    }
}

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // Настройка multicast
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    // Привязка сокета
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERVER_PORT);
    bind(sock, (sockaddr*)&addr, sizeof(addr));

    std::thread(send_discovery, sock).detach();
    std::thread(listen_responses, sock).detach();
    std::thread(check_inactive_clients).detach();
    std::thread(request_system_info, sock).detach();

    std::cout << "Server started. Monitoring clients...\n";
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
