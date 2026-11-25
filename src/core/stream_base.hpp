#pragma once

// Base comum de Transmitter e Receiver: dona da pipeline, do bus watch e do
// timer de medição.
//
// Threads:
//  - Pad probes rodam nas threads de streaming e só atualizam contadores
//    protegidos por mutex.
//  - Um timer (g_timeout_add) e o bus watch (gst_bus_add_watch) rodam no loop
//    principal do GLib: medem FPS/kbps, decidem o estado e chamam on_status /
//    on_stats. Nada toca na interface fora da thread principal.

#include <functional>
#include <mutex>
#include <string>

#include <gst/gst.h>

#include "config.hpp"

class StreamBase {
public:
    virtual ~StreamBase() { stop(); }
    StreamBase(const StreamBase&) = delete;
    StreamBase& operator=(const StreamBase&) = delete;

    // Leva a pipeline a GST_STATE_NULL e libera tudo. Pode ser chamado sempre.
    void stop();
    bool is_running() const { return pipeline_ != nullptr; }

    StreamState state() const { return state_; }
    const std::string& codec() const { return codec_; }         // encoder/decoder escolhido
    const std::string& last_error() const { return last_error_; }

    // Chamados na thread principal. Não chame stop() de dentro deles:
    // agende com g_idle_add (as GUIs fazem isso).
    std::function<void(StreamState, const std::string&)> on_status;
    std::function<void(const VideoStats&)> on_stats;

protected:
    StreamBase(bool is_tx, const char* tag) : is_tx_(is_tx), tag_(tag) {}

    // Liga bus e timer e pede PLAYING. Em falha, libera e devolve false.
    bool launch();
    bool fail(const std::string& error);   // registra o erro, libera, devolve false

    void add_network_probe(GstElement* e, const char* pad);
    void add_frame_probe(GstElement* e, const char* pad);

    GstElement* pipeline_ = nullptr;
    std::string codec_;
    const bool is_tx_;
    const char* tag_;

private:
    static gboolean on_bus_message(GstBus*, GstMessage* msg, gpointer self);
    static gboolean on_tick(gpointer self);
    static GstPadProbeReturn on_network_buffer(GstPad*, GstPadProbeInfo* info, gpointer self);
    static GstPadProbeReturn on_frame_buffer(GstPad*, GstPadProbeInfo* info, gpointer self);

    void set_state(StreamState s, const std::string& text = {});
    StreamState compute_state(gint64 now, uint64_t bytes, gint64 last_packet, gint64 last_frame) const;

    guint bus_watch_ = 0;
    guint tick_ = 0;
    StreamState state_ = StreamState::Stopped;
    bool pipeline_playing_ = false;
    bool failed_ = false;
    std::string last_error_;

    // Contadores das probes
    std::mutex mutex_;
    uint64_t frames_ = 0;
    uint64_t bytes_ = 0;
    gint64 last_packet_us_ = 0;
    gint64 last_frame_us_ = 0;

    // Janela de medição (thread principal)
    uint64_t win_frames_ = 0;
    uint64_t win_bytes_ = 0;
    gint64 win_start_us_ = 0;
    VideoStats stats_;
};

// Cria o elemento; se faltar o plugin, acrescenta o nome em 'missing'.
GstElement* make_element(const char* factory, const char* name, std::string& missing);

// Primeiro elemento disponível de cada lista (nullptr se nenhum).
const char* find_h264_encoder();
const char* find_h264_decoder();
