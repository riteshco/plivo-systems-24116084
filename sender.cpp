#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <vector>
#include <map>
#include <iostream>

#pragma pack(push, 1)
struct HarnessFrame {
    uint32_t seq;
    uint8_t payload[160];
};

struct PacketPiggy {
    uint16_t seq;
    uint8_t current[160];
    uint8_t previous[160];
};

struct PacketNormal {
    uint16_t seq;
    uint8_t current[160];
};

struct NackPacket {
    uint16_t seq;
};
#pragma pack(pop)

uint8_t history[65536][160];
bool has_history[65536];

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
        perror("bind 47010");
        return 1;
    }

    int nack_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in nack_addr = {0};
    nack_addr.sin_family = AF_INET;
    nack_addr.sin_port = htons(47004);
    nack_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(nack_fd, (struct sockaddr *)&nack_addr, sizeof nack_addr) < 0) {
        perror("bind 47004");
        return 1;
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay = {0};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    memset(has_history, 0, sizeof(has_history));

    struct pollfd fds[2];
    fds[0].fd = in_fd;
    fds[0].events = POLLIN;
    fds[1].fd = nack_fd;
    fds[1].events = POLLIN;

    for (;;) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) continue;

        if (fds[0].revents & POLLIN) {
            HarnessFrame frame;
            ssize_t n = recvfrom(in_fd, &frame, sizeof(frame), 0, NULL, NULL);
            if (n == sizeof(HarnessFrame)) {
                uint32_t net_seq;
                memcpy(&net_seq, &frame.seq, 4);
                uint32_t seq32 = ntohl(net_seq);
                uint16_t seq16 = seq32 & 0xFFFF;
                
                memcpy(history[seq16], frame.payload, 160);
                has_history[seq16] = true;

                // Send to relay
                if (seq16 % 7 == 0 || seq16 == 0 || !has_history[(uint16_t)(seq16 - 1)]) {
                    PacketNormal pkt;
                    pkt.seq = htons(seq16);
                    memcpy(pkt.current, frame.payload, 160);
                    sendto(out_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&relay, sizeof(relay));
                } else {
                    PacketPiggy pkt;
                    pkt.seq = htons(seq16);
                    memcpy(pkt.current, frame.payload, 160);
                    memcpy(pkt.previous, history[(uint16_t)(seq16 - 1)], 160);
                    sendto(out_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&relay, sizeof(relay));
                }
            }
        }

        if (fds[1].revents & POLLIN) {
            NackPacket nack;
            ssize_t n = recvfrom(nack_fd, &nack, sizeof(nack), 0, NULL, NULL);
            if (n == sizeof(NackPacket)) {
                uint16_t seq16 = ntohs(nack.seq);
                if (has_history[seq16]) {
                    PacketNormal pkt;
                    pkt.seq = htons(seq16);
                    memcpy(pkt.current, history[seq16], 160);
                    sendto(out_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&relay, sizeof(relay));
                }
            }
        }
    }
    return 0;
}
