#pragma once

// Servidor (transmissor): vídeo H.264 em MPEG-TS sobre UDP.
//
//   fonte -> videoconvert -> videoscale -> videorate -> I420 WxH@F
//     -> queue (leaky) -> x264enc tune=zerolatency speed-preset bitrate key-int-max=F
//     -> h264parse config-interval=-1 -> mpegtsmux alignment=7
//     -> udpsink host:porta sync=false
//
// Montada elemento a elemento (gst_element_factory_make + g_object_set),
// sem gst-launch. Compatível com o Receiver e com o VLC (udp://@:PORTA).

#include "stream_base.hpp"

class Transmitter : public StreamBase {
public:
    Transmitter() : StreamBase(true, "[SERVIDOR]") {}

    // Valida, monta e pede PLAYING. Não bloqueia. Em falha, veja last_error().
    bool start(const TransmitterConfig& cfg);
};
