#include "receiver.hpp"

#include <cstdio>
#include <string>

namespace {
bool has_property(gpointer obj, const char* prop) {
    return g_object_class_find_property(G_OBJECT_GET_CLASS(obj), prop) != nullptr;
}
}  // namespace

// tsdemux só cria o pad de vídeo depois de ler as tabelas PAT/PMT do fluxo.
void Receiver::on_pad_added(GstElement*, GstPad* pad, gpointer data) {
    auto* self = static_cast<Receiver*>(data);
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps)
        caps = gst_pad_query_caps(pad, nullptr);
    const std::string type = gst_structure_get_name(gst_caps_get_structure(caps, 0));
    gst_caps_unref(caps);

    if (type != "video/x-h264") {
        std::printf("%s Ignorando trilha '%s'.\n", self->tag_, type.c_str());
        return;
    }

    GstPad* sinkpad = gst_element_get_static_pad(self->video_queue_, "sink");
    if (!gst_pad_is_linked(sinkpad)) {
        if (GST_PAD_LINK_FAILED(gst_pad_link(pad, sinkpad)))
            std::fprintf(stderr, "%s Falha ao ligar a trilha H.264.\n", self->tag_);
        else
            std::printf("%s Trilha H.264 ligada.\n", self->tag_);
    }
    gst_object_unref(sinkpad);
}

bool Receiver::start(const ReceiverConfig& cfg, GstElement* video_sink) {
    // Ficamos com uma referência própria (não flutuante) até o sink entrar no bin.
    if (video_sink)
        gst_object_ref_sink(video_sink);

    auto drop_sink = [&] {
        if (video_sink)
            gst_object_unref(video_sink);
    };

    if (pipeline_) {
        drop_sink();
        return false;
    }

    std::string error;
    if (!validate(cfg, error)) {
        drop_sink();
        return fail(error);
    }

    const char* dec_name = find_h264_decoder();
    if (!dec_name) {
        drop_sink();
        return fail("Nenhum decoder H.264 disponível (instale gstreamer1-libav ou openh264)");
    }
    codec_ = dec_name;

    std::string missing;
    GstElement* src = make_element("udpsrc", "rede", missing);
    GstElement* q_net = make_element("queue", "fila_rede", missing);
    GstElement* demux = make_element("tsdemux", "demux", missing);
    GstElement* q_vid = make_element("queue", "fila_video", missing);
    GstElement* parse = make_element("h264parse", "parser", missing);
    GstElement* dec = make_element(dec_name, "decoder", missing);
    GstElement* conv = make_element("videoconvert", "conv", missing);
    if (!video_sink) {
        video_sink = make_element("autovideosink", "tela", missing);
        if (video_sink)
            gst_object_ref_sink(video_sink);
    }

    if (!missing.empty()) {
        for (GstElement* e : {src, q_net, demux, q_vid, parse, dec, conv})
            if (e) gst_object_unref(gst_object_ref_sink(e));
        drop_sink();
        return fail("Elementos do GStreamer ausentes: " + missing);
    }

    // UDP não carrega descrição de formato: informamos que é MPEG-TS.
    GstCaps* caps = gst_caps_from_string(MPEGTS_CAPS);
    g_object_set(src, "port", cfg.port, "caps", caps, "buffer-size", 2 * 1024 * 1024, nullptr);
    gst_caps_unref(caps);

    // sync=false: exibe assim que decodifica (menor atraso ao vivo).
    // async=false: a pipeline vai a PLAYING sem esperar o 1º quadro, por isso
    // PLAYING aqui NÃO significa vídeo recebido.
    if (has_property(video_sink, "sync"))
        g_object_set(video_sink, "sync", FALSE, nullptr);
    if (has_property(video_sink, "async"))
        g_object_set(video_sink, "async", FALSE, nullptr);

    pipeline_ = gst_pipeline_new("cliente-pipeline");
    gst_bin_add_many(GST_BIN(pipeline_), src, q_net, demux, q_vid, parse, dec, conv, video_sink, nullptr);
    gst_object_unref(video_sink);   // agora o bin é o dono

    if (!gst_element_link_many(src, q_net, demux, nullptr) ||
        !gst_element_link_many(q_vid, parse, dec, conv, video_sink, nullptr))
        return fail("Falha ao ligar os elementos do cliente");

    video_queue_ = q_vid;
    g_signal_connect(demux, "pad-added", G_CALLBACK(on_pad_added), this);

    add_network_probe(src, "src");   // pacotes recebidos
    add_frame_probe(conv, "src");    // quadros decodificados entregues ao sink

    std::printf("%s Escutando UDP %d (MPEG-TS/H.264, decoder %s)\n", tag_, cfg.port, dec_name);
    return launch();
}
