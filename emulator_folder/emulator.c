#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "link.h"
#include "logger.h"
#include "scenario.h"

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

enum emu_cli_opt {
    OPT_SCENARIO = 1000,
    OPT_SEED,
    OPT_SENDER_PORT,
    OPT_ACK_PORT,
    OPT_RECEIVER_IP,
    OPT_RECEIVER_PORT,
    OPT_BIND_IP,
    OPT_LOG_FILE,
    OPT_LINK_RATE,
    OPT_QUEUE_CAP,
    OPT_PROP_DELAY,
    OPT_JITTER,
    OPT_DATA_LOSS,
    OPT_ACK_LOSS,
    OPT_CORRUPT,
    OPT_DUPLICATE,
    OPT_REORDER,
    OPT_REORDER_TIMEOUT,
    OPT_HELP
};

struct emu_cli_config {
    const char *scenario_path;
    uint64_t seed;
    int sender_port;
    int ack_port;
    const char *receiver_ip;
    int receiver_port;
    const char *bind_ip;
    const char *log_file;

    bool has_link_rate;
    double link_rate;
    bool has_queue_cap;
    int queue_cap;
    bool has_prop_delay;
    double prop_delay;
    bool has_jitter;
    double jitter;
    bool has_data_loss;
    double data_loss;
    bool has_ack_loss;
    double ack_loss;
    bool has_corrupt;
    double corrupt;
    bool has_duplicate;
    double duplicate;
    bool has_reorder;
    double reorder;
    bool has_reorder_timeout;
    double reorder_timeout;
};

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  --scenario <path>       Scenario file (.scn)\n"
            "  --seed <uint64>         PRNG master seed (default: 42)\n"
            "  --senderPort <port>     Bind port for sender side (default: 5000)\n"
            "  --ackPort <port>        Bind port for receiver side (default: 8000)\n"
            "  --receiverIP <ip>       Receiver IP (default: 127.0.0.1)\n"
            "  --receiverPort <port>   Receiver listen port (default: 7000)\n"
            "  --bindIP <ip>           Local bind IP (default: 127.0.0.1)\n"
            "  --logFile <path>        Emulator CSV log path (default: emulator_log.csv)\n"
            "  --linkRate <bps>        Override link_rate_bps\n"
            "  --queueCap <pkts>       Override queue_capacity\n"
            "  --propDelay <ms>        Override prop_delay_ms\n"
            "  --jitter <ms>           Override jitter_ms\n"
            "  --dataLoss <prob>       Override data_loss\n"
            "  --ackLoss <prob>        Override ack_loss\n"
            "  --corrupt <prob>        Override corrupt probability\n"
            "  --duplicate <prob>      Override duplicate probability\n"
            "  --reorder <prob>        Override reorder probability\n"
            "  --reorderTimeout <ms>   Override reorder_timeout_ms\n",
            prog);
}

static int create_bound_udp_socket(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid IP address: %s\n", ip);
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    return fd;
}

static void init_sockaddr(struct sockaddr_in *addr, const char *ip, int port) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &addr->sin_addr);
}

static void apply_cli_overrides(struct emu_scenario *sc, const struct emu_cli_config *cfg) {
    for (int d = 0; d < 2; d++) {
        if (cfg->has_link_rate) {
            sc->dir[d].link_rate_bps = cfg->link_rate;
        }
        if (cfg->has_queue_cap) {
            sc->dir[d].queue_capacity = cfg->queue_cap;
        }
        if (cfg->has_prop_delay) {
            sc->dir[d].prop_delay_ms = cfg->prop_delay;
        }
        if (cfg->has_jitter) {
            sc->dir[d].jitter_ms = cfg->jitter;
        }
        if (cfg->has_corrupt) {
            sc->dir[d].corrupt_prob = cfg->corrupt;
        }
        if (cfg->has_duplicate) {
            sc->dir[d].dup_prob = cfg->duplicate;
        }
        if (cfg->has_reorder) {
            sc->dir[d].reorder_prob = cfg->reorder;
        }
        if (cfg->has_reorder_timeout) {
            sc->dir[d].reorder_timeout_ms = cfg->reorder_timeout;
        }
    }
    if (cfg->has_data_loss) {
        sc->dir[DIR_S2R].loss_prob = cfg->data_loss;
    }
    if (cfg->has_ack_loss) {
        sc->dir[DIR_R2S].loss_prob = cfg->ack_loss;
    }
}

static void drain_socket_dir(int fd, struct emu_state *st, enum emu_dir dir,
                             struct sockaddr_in *peer_addr, bool *have_peer) {
    uint8_t buf[EMU_MAX_PKT_BYTES];
    while (1) {
        struct sockaddr_in src;
        socklen_t srclen = sizeof(src);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&src, &srclen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom");
            break;
        }
        if (dir == DIR_S2R && peer_addr && have_peer) {
            *peer_addr = src;
            *have_peer = true;
        }
        emu_handle_arrival(st, dir, buf, (size_t)n, now_us());
    }
}

static void deliver_due_packets(struct emu_state *st, int sock_sender, int sock_recv,
                                const struct sockaddr_in *receiver_addr,
                                const struct sockaddr_in *sender_addr, bool have_sender) {
    while (1) {
        int64_t now = now_us();
        emu_check_reorder_timeouts(st, now);

        const struct scheduled_pkt *top = eq_peek(&st->eq);
        if (!top || top->deliver_us > now) {
            break;
        }

        struct scheduled_pkt pkt;
        eq_pop(&st->eq, &pkt);

        const char *dir_str = emu_dir_name(pkt.dir);
        const char *type_str = (pkt.orig_type != 255) ? emu_pkt_type_name(pkt.orig_type) : "RAW";
        long seq_log = (pkt.orig_type != 255) ? (long)pkt.orig_seq : -1L;

        if (pkt.dir == DIR_S2R) {
            ssize_t sent = sendto(sock_recv, pkt.data, pkt.len, 0,
                                  (const struct sockaddr *)receiver_addr, sizeof(*receiver_addr));
            if (sent < 0) {
                perror("sendto receiver");
            }
        } else if (have_sender) {
            ssize_t sent = sendto(sock_sender, pkt.data, pkt.len, 0,
                                  (const struct sockaddr *)sender_addr, sizeof(*sender_addr));
            if (sent < 0) {
                perror("sendto sender");
            }
        }

        int64_t delay_us = now - pkt.arrive_us;
        char detail[64];
        snprintf(detail, sizeof(detail), "delay_us=%lld;dup=%d",
                 (long long)delay_us, pkt.is_duplicate ? 1 : 0);
        log_emu_event(EMU_EV_DELIVER, dir_str, pkt.pkt_idx, type_str, seq_log, pkt.len, detail);
    }
}

int main(int argc, char *argv[]) {
    struct emu_cli_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.seed = 42;
    cfg.sender_port = 5000;
    cfg.ack_port = 8000;
    cfg.receiver_ip = "127.0.0.1";
    cfg.receiver_port = 7000;
    cfg.bind_ip = "127.0.0.1";
    cfg.log_file = "emulator_log.csv";

    static struct option long_opts[] = {
        {"scenario",       required_argument, NULL, OPT_SCENARIO},
        {"seed",           required_argument, NULL, OPT_SEED},
        {"senderPort",     required_argument, NULL, OPT_SENDER_PORT},
        {"ackPort",        required_argument, NULL, OPT_ACK_PORT},
        {"receiverIP",     required_argument, NULL, OPT_RECEIVER_IP},
        {"receiverPort",   required_argument, NULL, OPT_RECEIVER_PORT},
        {"bindIP",         required_argument, NULL, OPT_BIND_IP},
        {"logFile",        required_argument, NULL, OPT_LOG_FILE},
        {"linkRate",       required_argument, NULL, OPT_LINK_RATE},
        {"queueCap",       required_argument, NULL, OPT_QUEUE_CAP},
        {"propDelay",      required_argument, NULL, OPT_PROP_DELAY},
        {"jitter",         required_argument, NULL, OPT_JITTER},
        {"dataLoss",       required_argument, NULL, OPT_DATA_LOSS},
        {"ackLoss",        required_argument, NULL, OPT_ACK_LOSS},
        {"corrupt",        required_argument, NULL, OPT_CORRUPT},
        {"duplicate",      required_argument, NULL, OPT_DUPLICATE},
        {"reorder",        required_argument, NULL, OPT_REORDER},
        {"reorderTimeout", required_argument, NULL, OPT_REORDER_TIMEOUT},
        {"help",           no_argument,       NULL, OPT_HELP},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "", long_opts, NULL)) != -1) {
        switch (opt) {
            case OPT_SCENARIO:       cfg.scenario_path = optarg; break;
            case OPT_SEED:           cfg.seed = strtoull(optarg, NULL, 10); break;
            case OPT_SENDER_PORT:    cfg.sender_port = atoi(optarg); break;
            case OPT_ACK_PORT:       cfg.ack_port = atoi(optarg); break;
            case OPT_RECEIVER_IP:    cfg.receiver_ip = optarg; break;
            case OPT_RECEIVER_PORT:  cfg.receiver_port = atoi(optarg); break;
            case OPT_BIND_IP:        cfg.bind_ip = optarg; break;
            case OPT_LOG_FILE:       cfg.log_file = optarg; break;
            case OPT_LINK_RATE:      cfg.has_link_rate = true; cfg.link_rate = atof(optarg); break;
            case OPT_QUEUE_CAP:      cfg.has_queue_cap = true; cfg.queue_cap = atoi(optarg); break;
            case OPT_PROP_DELAY:     cfg.has_prop_delay = true; cfg.prop_delay = atof(optarg); break;
            case OPT_JITTER:         cfg.has_jitter = true; cfg.jitter = atof(optarg); break;
            case OPT_DATA_LOSS:      cfg.has_data_loss = true; cfg.data_loss = atof(optarg); break;
            case OPT_ACK_LOSS:       cfg.has_ack_loss = true; cfg.ack_loss = atof(optarg); break;
            case OPT_CORRUPT:        cfg.has_corrupt = true; cfg.corrupt = atof(optarg); break;
            case OPT_DUPLICATE:      cfg.has_duplicate = true; cfg.duplicate = atof(optarg); break;
            case OPT_REORDER:        cfg.has_reorder = true; cfg.reorder = atof(optarg); break;
            case OPT_REORDER_TIMEOUT:cfg.has_reorder_timeout = true; cfg.reorder_timeout = atof(optarg); break;
            case OPT_HELP:
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    struct emu_scenario sc;
    scenario_init_defaults(&sc);
    if (cfg.scenario_path != NULL) {
        if (!scenario_load_file(&sc, cfg.scenario_path)) {
            scenario_free(&sc);
            return EXIT_FAILURE;
        }
    }
    apply_cli_overrides(&sc, &cfg);

    if (!open_emu_logFile(cfg.log_file)) {
        scenario_free(&sc);
        return EXIT_FAILURE;
    }

    int sock_sender = create_bound_udp_socket(cfg.bind_ip, cfg.sender_port);
    int sock_recv = create_bound_udp_socket(cfg.bind_ip, cfg.ack_port);
    if (sock_sender < 0 || sock_recv < 0) {
        if (sock_sender >= 0) close(sock_sender);
        if (sock_recv >= 0) close(sock_recv);
        close_emu_logFile();
        scenario_free(&sc);
        return EXIT_FAILURE;
    }

    struct sockaddr_in receiver_addr;
    init_sockaddr(&receiver_addr, cfg.receiver_ip, cfg.receiver_port);

    struct sockaddr_in sender_addr;
    memset(&sender_addr, 0, sizeof(sender_addr));
    bool have_sender_addr = false;

    struct emu_state st;
    if (!emu_state_init(&st, &sc, cfg.seed)) {
        close(sock_sender);
        close(sock_recv);
        close_emu_logFile();
        scenario_free(&sc);
        return EXIT_FAILURE;
    }
    scenario_free(&sc);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sigset_t block_mask, empty_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGINT);
    sigaddset(&block_mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &block_mask, NULL);
    sigemptyset(&empty_mask);

    printf("Emulator ready: senderPort=%d ackPort=%d -> receiver=%s:%d seed=%llu\n",
           cfg.sender_port, cfg.ack_port, cfg.receiver_ip, cfg.receiver_port,
           (unsigned long long)cfg.seed);
    fflush(stdout);

    int maxfd = (sock_sender > sock_recv) ? sock_sender : sock_recv;

    while (!g_stop) {
        deliver_due_packets(&st, sock_sender, sock_recv, &receiver_addr, &sender_addr, have_sender_addr);
        if (g_stop) {
            break;
        }

        int64_t wakeup_us = emu_next_wakeup_us(&st);
        struct timespec ts;
        struct timespec *ts_ptr = NULL;
        if (wakeup_us >= 0) {
            int64_t now = now_us();
            int64_t delta_us = wakeup_us - now;
            if (delta_us < 0) {
                delta_us = 0;
            }
            ts.tv_sec = (time_t)(delta_us / 1000000LL);
            ts.tv_nsec = (long)((delta_us % 1000000LL) * 1000LL);
            ts_ptr = &ts;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock_sender, &rfds);
        FD_SET(sock_recv, &rfds);

        int ready = pselect(maxfd + 1, &rfds, NULL, NULL, ts_ptr, &empty_mask);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("pselect");
            break;
        }

        if (FD_ISSET(sock_sender, &rfds)) {
            drain_socket_dir(sock_sender, &st, DIR_S2R, &sender_addr, &have_sender_addr);
        }
        if (FD_ISSET(sock_recv, &rfds)) {
            drain_socket_dir(sock_recv, &st, DIR_R2S, NULL, NULL);
        }
    }

    emu_state_free(&st);
    close(sock_sender);
    close(sock_recv);
    close_emu_logFile();
    return 0;
}
