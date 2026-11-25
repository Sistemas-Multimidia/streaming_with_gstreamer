#include "stream_base.hpp"

#include <cstdio>

namespace {
constexpr guint TICK_MS = 250;
constexpr gint64 STATS_WINDOW_US = G_USEC_PER_SEC;

const char* first_available(std::initializer_list<const char*> names) {
    for (const char* n : names) {
        if (GstElementFactory* f = gst_element_factory_find(n)) {
            gst_object_unref(f);
            return n;
        }
    }
    return nullptr;
}
}  // namespace

GstElement* make_element(const char* factory, const char* name, std::string& missing) {
    GstElement* e = gst_element_factory_make(factory, name);
    if (!e) {
        if (!missing.empty())
            missing += ", ";
        missing += factory;
    }
    return e;
}

// x264enc é o padrão da disciplina; os outros são alternativas se faltar.
const char* find_h264_encoder() { return first_available({"x264enc", "openh264enc", "vah264enc"}); }
const char* find_h264_decoder() { return first_available({"avdec_h264", "openh264dec", "vah264dec"}); }

const char* state_name(StreamState s) {
    switch (s) {
    case StreamState::Stopped:      return "Parado";
    case StreamState::Starting:     return "Iniciando";
    case StreamState::Transmitting: return "Transmitindo";
    case StreamState::Waiting:      return "Aguardando pacotes";
    case StreamState::Receiving:    return "Recebendo (sem vídeo)";
    case StreamState::Playing:      return "Reproduzindo";
    case StreamState::NoSignal:     return "Sem sinal";
    case StreamState::Error:        return "Erro";
    }
    return "?";
}

// ---------------------------------------------------------------------------
void StreamBase::set_state(StreamState s, const std::string& text) {
    state_ = s;
    if (on_status)
        on_status(s, text);
}

bool StreamBase::fail(const std::string& error) {
    last_error_ = error;
    std::fprintf(stderr, "%s Erro: %s\n", tag_, error.c_str());
    stop();
    return false;
}

bool StreamBase::launch() {
    GstBus* bus = gst_element_get_bus(pipeline_);
    bus_watch_ = gst_bus_add_watch(bus, on_bus_message, this);

    {
        std::lock_guard<std::mutex> lk(mutex_);
        frames_ = bytes_ = 0;
        last_packet_us_ = last_frame_us_ = 0;
    }
    win_frames_ = win_bytes_ = 0;
    win_start_us_ = g_get_monotonic_time();
    stats_ = {};
    failed_ = false;
    pipeline_playing_ = false;
    state_ = StreamState::Starting;
    last_error_.clear();

    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        // A causa real (ex.: porta em uso) fica no bus.
        std::string msg = "Falha ao iniciar a pipeline";
        if (GstMessage* m = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
            GError* err = nullptr;
            gst_message_parse_error(m, &err, nullptr);
            msg += std::string(" (") + GST_OBJECT_NAME(m->src) + "): " + err->message;
            g_error_free(err);
            gst_message_unref(m);
        }
        gst_object_unref(bus);
        return fail(msg);
    }
    gst_object_unref(bus);

    tick_ = g_timeout_add(TICK_MS, on_tick, this);
    return true;
}

void StreamBase::stop() {
    if (tick_) {
        g_source_remove(tick_);
        tick_ = 0;
    }
    if (!pipeline_)
        return;
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    if (bus_watch_) {
        g_source_remove(bus_watch_);
        bus_watch_ = 0;
    }
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    state_ = StreamState::Stopped;
}

// ---------------------------------------------------------------------------
// Bus (thread principal)
// ---------------------------------------------------------------------------
gboolean StreamBase::on_bus_message(GstBus*, GstMessage* msg, gpointer data) {
    auto* self = static_cast<StreamBase*>(data);

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* dbg = nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        std::string text = std::string(GST_OBJECT_NAME(msg->src)) + ": " + err->message;
        std::fprintf(stderr, "%s Erro em %s\n%s Debug: %s\n", self->tag_, text.c_str(), self->tag_,
                     dbg ? dbg : "nenhum");
        g_error_free(err);
        g_free(dbg);
        self->failed_ = true;
        self->last_error_ = text;
        self->set_state(StreamState::Error, text);
        break;
    }
    case GST_MESSAGE_WARNING: {
        GError* err = nullptr;
        gst_message_parse_warning(msg, &err, nullptr);
        std::fprintf(stderr, "%s Aviso de %s: %s\n", self->tag_, GST_OBJECT_NAME(msg->src), err->message);
        g_error_free(err);
        break;
    }
    case GST_MESSAGE_EOS:
        self->failed_ = true;
        self->set_state(StreamState::Error, "Fim do fluxo (EOS)");
        break;
    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->pipeline_)) {
            GstState old_s, new_s;
            gst_message_parse_state_changed(msg, &old_s, &new_s, nullptr);
            std::printf("%s Pipeline: %s -> %s\n", self->tag_, gst_element_state_get_name(old_s),
                        gst_element_state_get_name(new_s));
            // Cliente: em PAUSED a porta já está aberta (o sink só completa
            // PLAYING quando chega o 1º quadro, se fizer preroll).
            self->pipeline_playing_ =
                self->is_tx_ ? new_s == GST_STATE_PLAYING : new_s >= GST_STATE_PAUSED;
        }
        break;
    default:
        break;
    }
    return G_SOURCE_CONTINUE;
}

// ---------------------------------------------------------------------------
// Medição e estado (thread principal)
// ---------------------------------------------------------------------------
StreamState StreamBase::compute_state(gint64 now, uint64_t bytes, gint64 last_packet,
                                      gint64 last_frame) const {
    if (!pipeline_playing_)
        return StreamState::Starting;
    if (is_tx_)
        return bytes > 0 ? StreamState::Transmitting : StreamState::Starting;
    if (last_packet == 0)
        return StreamState::Waiting;          // PLAYING, mas nada chegou pela rede
    if (now - last_packet > gint64(NO_SIGNAL_TIMEOUT_MS) * 1000)
        return StreamState::NoSignal;
    if (last_frame != 0 && now - last_frame < G_USEC_PER_SEC)
        return StreamState::Playing;
    // Chegam pacotes, mas nenhum quadro decodificado (ex.: aguardando um
    // quadro-chave com SPS/PPS, ou codec incompatível).
    return StreamState::Receiving;
}

gboolean StreamBase::on_tick(gpointer data) {
    auto* self = static_cast<StreamBase*>(data);
    if (self->failed_)
        return G_SOURCE_CONTINUE;

    const gint64 now = g_get_monotonic_time();
    uint64_t frames, bytes;
    gint64 last_packet, last_frame;
    {
        std::lock_guard<std::mutex> lk(self->mutex_);
        frames = self->frames_;
        bytes = self->bytes_;
        last_packet = self->last_packet_us_;
        last_frame = self->last_frame_us_;
    }

    bool window_done = false;
    if (now - self->win_start_us_ >= STATS_WINDOW_US) {
        const double dt = double(now - self->win_start_us_) / G_USEC_PER_SEC;
        self->stats_.fps = double(frames - self->win_frames_) / dt;
        self->stats_.kbps = double(bytes - self->win_bytes_) * 8.0 / 1000.0 / dt;
        self->win_frames_ = frames;
        self->win_bytes_ = bytes;
        self->win_start_us_ = now;
        window_done = true;
    }
    self->stats_.frames = frames;
    self->stats_.bytes = bytes;

    const StreamState s = self->compute_state(now, bytes, last_packet, last_frame);
    if (s != self->state_)
        self->set_state(s);
    if (window_done && self->on_stats)
        self->on_stats(self->stats_);
    return G_SOURCE_CONTINUE;
}

// ---------------------------------------------------------------------------
// Probes (threads de streaming)
// ---------------------------------------------------------------------------
GstPadProbeReturn StreamBase::on_network_buffer(GstPad*, GstPadProbeInfo* info, gpointer data) {
    auto* self = static_cast<StreamBase*>(data);
    gsize n = 0;
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER)
        n = gst_buffer_get_size(GST_PAD_PROBE_INFO_BUFFER(info));
    else if (info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST)
        n = gst_buffer_list_calculate_size(GST_PAD_PROBE_INFO_BUFFER_LIST(info));

    std::lock_guard<std::mutex> lk(self->mutex_);
    self->bytes_ += n;
    self->last_packet_us_ = g_get_monotonic_time();
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn StreamBase::on_frame_buffer(GstPad*, GstPadProbeInfo*, gpointer data) {
    auto* self = static_cast<StreamBase*>(data);
    std::lock_guard<std::mutex> lk(self->mutex_);
    self->frames_++;
    self->last_frame_us_ = g_get_monotonic_time();
    return GST_PAD_PROBE_OK;
}

void StreamBase::add_network_probe(GstElement* e, const char* pad_name) {
    GstPad* pad = gst_element_get_static_pad(e, pad_name);
    gst_pad_add_probe(pad, GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST),
                      on_network_buffer, this, nullptr);
    gst_object_unref(pad);
}

void StreamBase::add_frame_probe(GstElement* e, const char* pad_name) {
    GstPad* pad = gst_element_get_static_pad(e, pad_name);
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, on_frame_buffer, this, nullptr);
    gst_object_unref(pad);
}
