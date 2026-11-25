// test_core.cpp — teste automático do núcleo (sem GTK).
//
// 1. Validação de campos inválidos.
// 2. Cliente sozinho: "Aguardando pacotes" (pipeline ativa, mas sem vídeo).
// 3. Servidor -> cliente em 127.0.0.1: "Transmitindo" / "Reproduzindo", FPS ~ configurado.
// 4. Servidor parado: cliente vai para "Sem sinal".
// 5. Servidor reiniciado: cliente volta a reproduzir.
//
// Uso: ./test_core [porta]

#include <cstdio>
#include <cstdlib>
#include <string>

#include "receiver.hpp"
#include "transmitter.hpp"

namespace {

GMainLoop* loop = nullptr;
int failures = 0;

struct Track {
    const char* name;
    StreamState last = StreamState::Stopped;
    double max_fps = 0, max_kbps = 0;

    void attach(StreamBase& s) {
        s.on_status = [this](StreamState st, const std::string& msg) {
            if (st != last)
                std::printf("  [%s] %s%s%s\n", name, state_name(st), msg.empty() ? "" : " — ", msg.c_str());
            last = st;
        };
        s.on_stats = [this](const VideoStats& v) {
            if (v.fps > max_fps) max_fps = v.fps;
            if (v.kbps > max_kbps) max_kbps = v.kbps;
        };
    }
};

void run_for(guint ms) {
    g_timeout_add(ms, [](gpointer) -> gboolean { g_main_loop_quit(loop); return G_SOURCE_REMOVE; }, nullptr);
    g_main_loop_run(loop);
}

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        failures++;
}

}  // namespace

int main(int argc, char** argv) {
    gst_init(&argc, &argv);
    const int port = argc > 1 ? std::atoi(argv[1]) : 5600;
    loop = g_main_loop_new(nullptr, FALSE);

    std::printf("encoder: %s | decoder: %s\n", find_h264_encoder() ? find_h264_encoder() : "(nenhum)",
                find_h264_decoder() ? find_h264_decoder() : "(nenhum)");

    // 1. Validação
    std::string err;
    TransmitterConfig bad;
    bad.host = "300.1.1.1";
    check(!validate(bad, err), "rejeita IP inválido");
    bad = {};
    bad.port = 70000;
    check(!validate(bad, err), "rejeita porta inválida");
    bad = {};
    bad.bitrate_kbps = 10;
    check(!validate(bad, err), "rejeita bitrate fora da faixa");
    bad = {};
    bad.preset = "turbo";
    check(!validate(bad, err), "rejeita preset desconhecido");
    {
        Transmitter t;
        check(!t.start(bad) && !t.last_error().empty() && !t.is_running(),
              "start() com config inválida falha sem deixar pipeline");
    }

    // 2. Cliente sozinho
    Receiver rx;
    Track rt{"cliente"};
    rt.attach(rx);
    ReceiverConfig rcfg;
    rcfg.port = port;
    check(rx.start(rcfg, gst_element_factory_make("fakesink", nullptr)), "cliente inicia");
    run_for(1500);
    check(rt.last == StreamState::Waiting, "cliente sem servidor fica em 'Aguardando pacotes'");

    // 3. Servidor -> cliente
    Transmitter tx;
    Track tt{"servidor"};
    tt.attach(tx);
    TransmitterConfig tcfg;
    tcfg.port = port;
    check(tx.start(tcfg), "servidor inicia");
    run_for(5000);
    check(tt.last == StreamState::Transmitting, "servidor em 'Transmitindo'");
    check(rt.last == StreamState::Playing, "cliente em 'Reproduzindo'");
    std::printf("  servidor: até %.1f fps, %.0f kbps | cliente: até %.1f fps, %.0f kbps\n", tt.max_fps,
                tt.max_kbps, rt.max_fps, rt.max_kbps);
    check(rt.max_fps > VIDEO_FPS * 0.8, "cliente exibe perto do FPS configurado");

    // 4. Servidor parado
    tx.stop();
    run_for(NO_SIGNAL_TIMEOUT_MS + 1000);
    check(rt.last == StreamState::NoSignal, "cliente detecta 'Sem sinal' após o servidor parar");

    // 5. Reinício
    check(tx.start(tcfg), "servidor reinicia");
    run_for(4000);
    check(rt.last == StreamState::Playing, "cliente volta a reproduzir após reinício");
    tx.stop();

    rx.stop();
    g_main_loop_unref(loop);
    std::printf("\n%s (%d falha(s))\n", failures ? "FALHOU" : "OK", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
