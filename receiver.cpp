#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <map>

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

bool received[65536];
double last_nack_time_ms[65536];

double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

double t0_val = 0;
uint32_t highest_seq32 = 0;
int player_fd;
struct sockaddr_in player_addr;
int nack_fd;
struct sockaddr_in nack_addr;

void check_nacks(double now) {
    uint32_t max_seq = highest_seq32;
    uint32_t start = (max_seq > 50) ? (max_seq - 50) : 0;

    for (uint32_t s = start; s < max_seq; s++) {
        uint16_t s16 = s & 0xFFFF;
        if (!received[s16]) {
            double now_ms = now * 1000.0;
            if (now_ms - last_nack_time_ms[s16] > 60.0) { // 60ms timeout between NACKs
                last_nack_time_ms[s16] = now_ms;
                NackPacket nack;
                nack.seq = htons(s16);
                sendto(nack_fd, &nack, sizeof(nack), 0, (struct sockaddr *)&nack_addr, sizeof(nack_addr));
            }
        }
    }
}

int main(void) {
    if (getenv("T0")) t0_val = atof(getenv("T0"));

    for (int i = 0; i < 65536; i++) {
        received[i] = false;
        last_nack_time_ms[i] = 0;
    }

    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof in_addr) < 0) {
        perror("bind 47002");
        return 1;
    }

    nack_fd = socket(AF_INET, SOCK_DGRAM, 0);
    nack_addr = {0};
    nack_addr.sin_family = AF_INET;
    nack_addr.sin_port = htons(47003);
    nack_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    player_fd = socket(AF_INET, SOCK_DGRAM, 0);
    player_addr = {0};
    player_addr.sin_family = AF_INET;
    player_addr.sin_port = htons(47020);
    player_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    unsigned char buf[2048];
    struct pollfd fds[1];
    fds[0].fd = in_fd;
    fds[0].events = POLLIN;

    for (;;) {
        int ret = poll(fds, 1, 10); // 10ms timeout
        if (ret > 0 && (fds[0].revents & POLLIN)) {
            ssize_t n = recvfrom(in_fd, buf, sizeof buf, 0, NULL, NULL);
            if (n == sizeof(PacketNormal) || n == sizeof(PacketPiggy)) {
                uint16_t seq16;
                memcpy(&seq16, buf, 2);
                seq16 = ntohs(seq16);
                
                uint32_t h = highest_seq32;
                uint32_t seq32 = (h & 0xFFFF0000) | seq16;
                if (seq32 + 0x8000 < h) seq32 += 0x10000;
                else if (h > 0x8000 && seq32 > h + 0x8000) seq32 -= 0x10000;

                if (seq32 > h || h == 0) {
                    highest_seq32 = seq32;
                }

                if (!received[seq16]) {
                    received[seq16] = true;
                    HarnessFrame frame;
                    uint32_t net_seq = htonl(seq32);
                    memcpy(&frame.seq, &net_seq, 4);
                    memcpy(frame.payload, buf + 2, 160);
                    sendto(player_fd, &frame, sizeof(frame), 0, (struct sockaddr *)&player_addr, sizeof(player_addr));
                }

                if (n == sizeof(PacketPiggy)) {
                    uint16_t prev16 = (seq16 - 1) & 0xFFFF;
                    if (!received[prev16]) {
                        received[prev16] = true;
                        HarnessFrame frame;
                        uint32_t net_seq = htonl(seq32 - 1);
                        memcpy(&frame.seq, &net_seq, 4);
                        memcpy(frame.payload, buf + 2 + 160, 160);
                        sendto(player_fd, &frame, sizeof(frame), 0, (struct sockaddr *)&player_addr, sizeof(player_addr));
                    }
                }
            }
        }
        check_nacks(get_time());
    }
    return 0;
}
