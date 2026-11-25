#pragma once

// Cliente (receptor): vídeo H.264 em MPEG-TS sobre UDP.
//
//   udpsrc porta (caps video/mpegts) -> queue -> tsdemux ~(pad-added)~>
//     queue -> h264parse -> decoder H.264 -> videoconvert -> sink de vídeo
//
// O estado (Aguardando / Recebendo / Reproduzindo / Sem sinal) vem de probes
// que contam pacotes UDP e quadros decodificados — não do GstState PLAYING.

#include "stream_base.hpp"

class Receiver : public StreamBase {
public:
    Receiver() : StreamBase(false, "[CLIENTE]") {}

    // video_sink: sink a usar (ex.: gtk4paintablesink para exibir dentro da
    // janela). A função assume a referência flutuante. nullptr -> autovideosink
    // em janela separada.
    bool start(const ReceiverConfig& cfg, GstElement* video_sink = nullptr);

private:
    static void on_pad_added(GstElement*, GstPad* pad, gpointer self);
    GstElement* video_queue_ = nullptr;   // destino do pad de vídeo do tsdemux
};
