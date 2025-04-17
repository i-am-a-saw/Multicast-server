#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <vector>
#include <string>

#pragma comment(lib,"ws2_32.lib")

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
                    sprintf_s(addr, "%d.%d.%d.%d", a, b, c, d);
                    addresses.push_back(addr);
                    //printf("%d\n", c);

                    // Ограничим количество для демонстрации
                    if (addresses.size() >= MAX_ADRESSES) return addresses;
                }
            }
        }
    }
    return addresses;
}

void multicastSender(SOCKET mcastSocket) {
    auto addresses = generateMulticastAddresses();
    int port = MULTICAST_START_PORT;

    while (keepMulticasting) {
        for (const auto& addr : addresses) {
            sockaddr_in mcastAddr;
            mcastAddr.sin_family = AF_INET;
            mcastAddr.sin_addr.s_addr = inet_addr(addr.c_str());
            mcastAddr.sin_port = htons(port);

            char message[256];
            sprintf_s(message, "Multicast from server to %s:%d", addr.c_str(), port);

            if (sendto(mcastSocket, message, strlen(message), 0,
                (struct sockaddr*)&mcastAddr, sizeof(mcastAddr)) == SOCKET_ERROR) {
                printf("Failed to send to %s:%d (Error: %d)\n", addr.c_str(), port, WSAGetLastError());
                continue;
            }

            printf("Sent multicast to %s:%d\n", addr.c_str(), port);

            // Небольшая пауза между отправками
            Sleep(100);

            if (!keepMulticasting) break;
        }

        // Смена порта для следующего цикла
        port = (port % 65535) + 1;

        // Ожидание перед следующим циклом рассылки
        Sleep(MULTICAST_INTERVAL);
    }
    printf("Multicast sender stopped\n");
}

int main() {
    SOCKET s, mcastSocket;
    struct sockaddr_in server, si_other;
    int slen = sizeof(si_other);
    char buf[BUFLEN];
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    // Основной сокет для эхо-сервера
    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    if (bind(s, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(s);
        WSACleanup();
        return 1;
    }

    // Сокет для multicast
    if ((mcastSocket = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        printf("Multicast socket creation failed: %d\n", WSAGetLastError());
        closesocket(s);
        WSACleanup();
        return 1;
    }

    // Разрешаем сокету отправлять multicast
    unsigned char ttl = 1;
    if (setsockopt(mcastSocket, IPPROTO_IP, IP_MULTICAST_TTL,
        (const char*)&ttl, sizeof(ttl)) == SOCKET_ERROR) {
        printf("setsockopt(IP_MULTICAST_TTL) failed: %d\n", WSAGetLastError());
    }

    // Разрешаем множественные привязки
    BOOL reuse = TRUE;
    if (setsockopt(mcastSocket, SOL_SOCKET, SO_REUSEADDR,
        (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR) {
        printf("setsockopt(SO_REUSEADDR) failed: %d\n", WSAGetLastError());
    }

    std::thread mcastThread(multicastSender, mcastSocket);
    mcastThread.detach();

    printf("Server started. Waiting for data...\n");
    printf("Multicast sending to multiple addresses\n");

    while (true) {
        memset(buf, '\0', BUFLEN);

        int recv_len;
        if ((recv_len = recvfrom(s, buf, BUFLEN, 0,
            (struct sockaddr*)&si_other, &slen)) == SOCKET_ERROR) {
            printf("recvfrom() failed: %d\n", WSAGetLastError());
            break;
        }

        printf("Received from %s:%d: %s\n",
            inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port), buf);

        if (sendto(s, buf, recv_len, 0,
            (struct sockaddr*)&si_other, slen) == SOCKET_ERROR) {
                printf("sendto() failed: %d\n", WSAGetLastError());
        }
    }

    keepMulticasting = false;
    closesocket(mcastSocket);
    closesocket(s);
    WSACleanup();

    return 0;
}