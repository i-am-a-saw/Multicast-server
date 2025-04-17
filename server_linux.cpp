#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <errno.h>

#define BUFLEN 512
#define PORT 8888
#define MULTICAST_START_PORT 8889
#define MULTICAST_INTERVAL 5000
#define MAX_ADRESSES 1000

std::atomic<bool> keepMulticasting(true);

// Генерация диапазона multicast адресов
std::vector<std::string> generateMulticastAddresses() {
    std::vector<std::string> addresses;

    // Диапазон class D: 224.0.0.0 - 239.255.255.255
    for (int a = 224; a <= 239; a++) {
        for (int b = 0; b <= 255; b++) {
            for (int c = 0; c <= 255; c++) {
                for (int d = 0; d <= 255; d++) {
                    // Пропускаем зарезервированные адреса
                    if (a == 224 && b == 0 && c == 0) continue; // Локальные multicast
                    if (a == 239 && b == 255 && c == 255 && d == 255) continue; // Последний адрес

                    char addr[16];
                    snprintf(addr, sizeof(addr), "%d.%d.%d.%d", a, b, c, d);
                    addresses.push_back(addr);

                    // Ограничим количество для демонстрации
                    if (addresses.size() >= MAX_ADRESSES) return addresses;
                }
            }
        }
    }
    return addresses;
}

void multicastSender(int mcastSocket) {
    auto addresses = generateMulticastAddresses();
    int port = MULTICAST_START_PORT;

    while (keepMulticasting) {
        for (const auto& addr : addresses) {
            sockaddr_in mcastAddr;
            mcastAddr.sin_family = AF_INET;
            mcastAddr.sin_addr.s_addr = inet_addr(addr.c_str());
            mcastAddr.sin_port = htons(port);

            char message[256];
            snprintf(message, sizeof(message), "Multicast from server to %s:%d", addr.c_str(), port);

            if (sendto(mcastSocket, message, strlen(message), 0,
                (struct sockaddr*)&mcastAddr, sizeof(mcastAddr)) == -1) {
                printf("Failed to send to %s:%d (Error: %s)\n", addr.c_str(), port, strerror(errno));
                continue;
            }

            printf("Sent multicast to %s:%d\n", addr.c_str(), port);

            // Небольшая пауза между отправками
            usleep(100 * 1000); // 100ms

            if (!keepMulticasting) break;
        }

        // Смена порта для следующего цикла
        port = (port % 65535) + 1;

        // Ожидание перед следующим циклом рассылки
        usleep(MULTICAST_INTERVAL * 1000);
    }
    printf("Multicast sender stopped\n");
}

int main() {
    int s, mcastSocket;
    struct sockaddr_in server, si_other;
    socklen_t slen = sizeof(si_other);
    char buf[BUFLEN];

    // Основной сокет для эхо-сервера
    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        printf("Socket creation failed: %s\n", strerror(errno));
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    if (bind(s, (struct sockaddr*)&server, sizeof(server)) == -1) {
        printf("Bind failed: %s\n", strerror(errno));
        close(s);
        return 1;
    }

    // Сокет для multicast
    if ((mcastSocket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        printf("Multicast socket creation failed: %s\n", strerror(errno));
        close(s);
        return 1;
    }

    // Разрешаем сокету отправлять multicast
    unsigned char ttl = 1;
    if (setsockopt(mcastSocket, IPPROTO_IP, IP_MULTICAST_TTL,
        &ttl, sizeof(ttl)) == -1) {
        printf("setsockopt(IP_MULTICAST_TTL) failed: %s\n", strerror(errno));
    }

    // Разрешаем множественные привязки
    int reuse = 1;
    if (setsockopt(mcastSocket, SOL_SOCKET, SO_REUSEADDR,
        &reuse, sizeof(reuse)) == -1) {
        printf("setsockopt(SO_REUSEADDR) failed: %s\n", strerror(errno));
    }

    std::thread mcastThread(multicastSender, mcastSocket);
    mcastThread.detach();

    printf("Server started. Waiting for data...\n");
    printf("Multicast sending to multiple addresses\n");

    while (true) {
        memset(buf, '\0', BUFLEN);

        int recv_len;
        if ((recv_len = recvfrom(s, buf, BUFLEN, 0,
            (struct sockaddr*)&si_other, &slen)) == -1) {
            printf("recvfrom() failed: %s\n", strerror(errno));
            break;
        }

        printf("Received from %s:%d: %s\n",
            inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port), buf);

        if (sendto(s, buf, recv_len, 0,
            (struct sockaddr*)&si_other, slen) == -1) {
                printf("sendto() failed: %s\n", strerror(errno));
        }
    }

    keepMulticasting = false;
    close(mcastSocket);
    close(s);

    return 0;
}
