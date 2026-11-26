#include "transmitter.hpp"

#include <cstdio>
#include <initializer_list>
#include <string>

namespace {

bool has_property(gpointer obj, const char* prop) {
    return g_object_class_find_property(G_OBJECT_GET_CLASS(obj), prop) != nullptr;
}

void configure_encoder(GstElement* enc, const std::string& factory, const TransmitterConfig& cfg) {
    if (factory == "x264enc") {
        gst_util_set_object_arg(G_OBJECT(enc), "tune", "zerolatency");
        gst_util_set_object_arg(G_OBJECT(enc), "speed-preset", cfg.preset.c_str());
        g_object_set(enc,
                     "bitrate", guint(cfg.bitrate_kbps),   // kbit/s
                     "key-int-max", guint(cfg.fps),        // 1 quadro-chave por segundo
                     nullptr);
    } else if (factory == "openh264enc") {
        // Sem presets: aproxima pela "complexity".
        const char* complexity = cfg.preset == "medium" ? "high"
                                 : cfg.preset == "fast"  ? "medium"
                                                         : "low";
        g_object_set(enc, "bitrate", guint(cfg.bitrate_kbps) * 1000,  // bit/s
                     "gop-size", guint(cfg.fps), nullptr);
        gst_util_set_object_arg(G_OBJECT(enc), "complexity", complexity);
        gst_util_set_object_arg(G_OBJECT(enc), "rate-control", "bitrate");
    } else {
        if (has_property(enc, "bitrate"))
            g_object_set(enc, "bitrate", guint(cfg.bitrate_kbps), nullptr);
        if (has_property(enc, "key-int-max"))
            g_object_set(enc, "key-int-max", guint(cfg.fps), nullptr);
    }
}

}  // namespace

bool Transmitter::start(const TransmitterConfig& cfg) {
    if (pipeline_)
        return false;

    std::string error;
    if (!validate(cfg, error))
        return fail(error);

    const char* enc_name = find_h264_encoder();
    if (!enc_name)
        return fail("Nenhum encoder H.264 disponível (instale gstreamer1-plugins-ugly ou openh264)");
    codec_ = enc_name;

    const bool cam = cfg.source == MediaSource::Camera;
    std::string missing;
    GstElement* src = make_element(cam ? "autovideosrc" : "videotestsrc", "fonte", missing);
    GstElement* conv = make_element("videoconvert", "conv", missing);
    GstElement* scale = make_element("videoscale", "escala", missing);
    GstElement* rate = make_element("videorate", "taxa", missing);
    GstElement* raw_caps = make_element("capsfilter", "formato", missing);
    GstElement* queue = make_element("queue", "fila", missing);
    GstElement* enc = make_element(enc_name, "encoder", missing);
    GstElement* parse = make_element("h264parse", "parser", missing);
    GstElement* mux = make_element("mpegtsmux", "mux", missing);
    GstElement* sink = make_element("udpsink", "rede", missing);

    if (!missing.empty()) {
        for (GstElement* e : {src, conv, scale, rate, raw_caps, queue, enc, parse, mux, sink})
            if (e) gst_object_unref(gst_object_ref_sink(e));
        return fail("Elementos do GStreamer ausentes: " + missing);
    }
    pipeline_ = gst_pipeline_new("servidor-pipeline");

    if (!cam) {
        g_object_set(src, "is-live", TRUE, nullptr);
        gst_util_set_object_arg(G_OBJECT(src), "pattern", "ball");
    }

    GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "I420",
                                        "width", G_TYPE_INT, cfg.width,
                                        "height", G_TYPE_INT, cfg.height,
                                        "framerate", GST_TYPE_FRACTION, cfg.fps, 1,
                                        nullptr);
    g_object_set(raw_caps, "caps", caps, nullptr);
    gst_caps_unref(caps);

    // Fila curta e "leaky": se o encoder atrasar, descarta quadros antigos em
    // vez de acumular atraso.
    g_object_set(queue, "max-size-buffers", 3u, "max-size-bytes", 0u, "max-size-time", guint64(0), nullptr);
    gst_util_set_object_arg(G_OBJECT(queue), "leaky", "downstream");

    configure_encoder(enc, enc_name, cfg);

    // -1: reenvia SPS/PPS em todo quadro-chave -> um cliente que entra no meio
    // do fluxo consegue decodificar (causa comum de "ausência de vídeo").
    g_object_set(parse, "config-interval", -1, nullptr);
    g_object_set(mux, "alignment", MPEGTS_ALIGNMENT, nullptr);
    g_object_set(sink, "host", cfg.host.c_str(), "port", cfg.port, "sync", FALSE, "async", FALSE, nullptr);

    gst_bin_add_many(GST_BIN(pipeline_), src, conv, scale, rate, raw_caps, queue, enc, parse, mux, sink,
                     nullptr);
    if (!gst_element_link_many(src, conv, scale, rate, raw_caps, queue, enc, parse, mux, sink, nullptr))
        return fail("Falha ao ligar os elementos do servidor");

    add_frame_probe(enc, "src");      // quadros codificados
    add_network_probe(sink, "sink");  // bytes enviados

    std::printf("%s %s:%d  %dx%d@%d  %s %d kbps preset=%s\n", tag_, cfg.host.c_str(), cfg.port, cfg.width,
                cfg.height, cfg.fps, enc_name, cfg.bitrate_kbps, cfg.preset.c_str());
    return launch();
}
