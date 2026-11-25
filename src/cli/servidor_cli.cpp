// servidor_cli — transmissor de vídeo H.264/MPEG-TS/UDP sem interface gráfica.
// Uso: ./servidor_cli [ip=127.0.0.1] [porta=5000] [bitrate_kbps=500] [preset=ultrafast] [test|cam]

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "transmitter.hpp"

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

    TransmitterConfig cfg;
    if (argc > 1) cfg.host = argv[1];
    if (argc > 2) cfg.port = std::atoi(argv[2]);
    if (argc > 3) cfg.bitrate_kbps = std::atoi(argv[3]);
    if (argc > 4) cfg.preset = argv[4];
    if (argc > 5) cfg.source = std::strcmp(argv[5], "cam") == 0 ? MediaSource::Camera : MediaSource::Test;

    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    std::signal(SIGINT, on_sigint);
    g_timeout_add(200, check_quit, loop);

    Transmitter tx;
    tx.on_status = [loop](StreamState s, const std::string& msg) {
        std::printf("[TX] %s%s%s\n", state_name(s), msg.empty() ? "" : " — ", msg.c_str());
        if (s == StreamState::Error)
            g_main_loop_quit(loop);
    };
    tx.on_stats = [](const VideoStats& v) {
        std::printf("[TX] %.1f quadros/s  %.0f kbps  %llu bytes\n", v.fps, v.kbps, (unsigned long long)v.bytes);
    };
    if (!tx.start(cfg)) {
        std::fprintf(stderr, "%s\n", tx.last_error().c_str());
        g_main_loop_unref(loop);
        return 1;
    }

    std::printf("Transmitindo para %s:%d (Ctrl+C para sair)\n", cfg.host.c_str(), cfg.port);
    g_main_loop_run(loop);

    tx.stop();
    g_main_loop_unref(loop);
    return 0;
}
