// cliente_cli — receptor de vídeo H.264/MPEG-TS/UDP sem interface gráfica
// (o vídeo abre em janela separada via autovideosink).
// Uso: ./cliente_cli [porta=5000]

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>

#include "receiver.hpp"

static std::atomic<bool> g_quit{false};

static void on_sigint(int) { g_quit = true; }

static gboolean check_quit(gpointer loop) {
    if (g_quit) {
        g_main_loop_quit(static_cast<GMainLoop*>(loop));
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

int main(int argc, char** argv) {
    gst_init(&argc, &argv);

    ReceiverConfig cfg;
    if (argc > 1) cfg.port = std::atoi(argv[1]);

    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    std::signal(SIGINT, on_sigint);
    g_timeout_add(200, check_quit, loop);

    Receiver rx;
    rx.on_status = [loop](StreamState s, const std::string& msg) {
        std::printf("[RX] %s%s%s\n", state_name(s), msg.empty() ? "" : " — ", msg.c_str());
        if (s == StreamState::Error)
            g_main_loop_quit(loop);
    };
    rx.on_stats = [](const VideoStats& v) {
        std::printf("[RX] %.1f quadros/s  %.0f kbps  %llu bytes\n", v.fps, v.kbps, (unsigned long long)v.bytes);
    };
    if (!rx.start(cfg)) {
        std::fprintf(stderr, "%s\n", rx.last_error().c_str());
        g_main_loop_unref(loop);
        return 1;
    }

    std::printf("Escutando UDP %d (Ctrl+C para sair)\n", cfg.port);
    g_main_loop_run(loop);

    rx.stop();
    g_main_loop_unref(loop);
    return 0;
}
