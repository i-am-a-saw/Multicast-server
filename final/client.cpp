#include <iostream>
#include <string>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sstream>
#include <thread>

#define MULTICAST_GROUP "224.1.1.1"
#define MULTICAST_PORT 8889
#define SERVER_PORT 8888

std::string get_system_info() {
    struct sysinfo info;
    sysinfo(&info);
    std::ostringstream oss;
    oss << "CPU: " << info.loads[0] / 65536.0 << "% "
        << "RAM: " << (info.totalram - info.freeram) / (1024 * 1024) << "MB/" 
        << info.totalram / (1024 * 1024) << "MB";
    return oss.str();
}

void listen_multicast(int response_sock) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return;
    }

    // Подписка на multicast
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    // Привязка
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(MULTICAST_PORT);
    bind(sock, (sockaddr*)&addr, sizeof(addr));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    char buf[1024];
    while (true) {
        sockaddr_in sender{};
        socklen_t len = sizeof(sender);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&sender, &len);
        if (n <= 0) continue;

        buf[n] = '\0';
        std::string msg(buf);

        if (msg.find("DISCOVERY_SERVER=") != std::string::npos) {
            server_addr.sin_addr = sender.sin_addr;
            std::string response = "CLIENT_ID=" + std::to_string(getpid());
            sendto(response_sock, response.c_str(), response.size(), 0,
                  (sockaddr*)&server_addr, sizeof(server_addr));
        }
        else if (msg == "GET_SYSTEM_INFO") {
            std::string info = "SYSINFO:" + get_system_info();
            sendto(response_sock, info.c_str(), info.size(), 0,
                  (sockaddr*)&server_addr, sizeof(server_addr));
        }
    }
}

int main() {
    int response_sock = socket(AF_INET, SOCK_DGRAM, 0);
    std::thread(listen_multicast, response_sock).detach();

    std::cout << "Client started. PID: " << getpid() << "\n";
    while (true) std::this_thread::sleep_for(std::chrono::seconds(10));
}
