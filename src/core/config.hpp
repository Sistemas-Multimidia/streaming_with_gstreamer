#pragma once

// Parte 2 — configuração do streaming de vídeo H.264 em MPEG-TS/UDP.
// Padrões da pipeline MPEG-TS/UDP com as melhorias da Parte 1.3
// (key-int-max, config-interval=-1, alignment=7, sync=false).

#include <cstdint>
#include <string>

constexpr const char* DEFAULT_HOST = "127.0.0.1";
constexpr int DEFAULT_PORT = 5000;

constexpr int VIDEO_WIDTH = 640;
constexpr int VIDEO_HEIGHT = 360;
constexpr int VIDEO_FPS = 25;
constexpr int VIDEO_BITRATE_KBPS = 500;
constexpr const char* VIDEO_PRESET = "ultrafast";

constexpr int VIDEO_MIN_BITRATE_KBPS = 100;
constexpr int VIDEO_MAX_BITRATE_KBPS = 8000;

// MPEG-TS: pacotes de 188 bytes; alignment=7 -> 7*188 = 1316 B por datagrama,
// abaixo do MTU de 1500 (sem fragmentação IP).
constexpr const char* MPEGTS_CAPS = "video/mpegts,systemstream=(boolean)true,packetsize=(int)188";
constexpr int MPEGTS_ALIGNMENT = 7;

// Cliente: sem pacotes por este tempo -> "Sem sinal".
constexpr int NO_SIGNAL_TIMEOUT_MS = 2000;

enum class MediaSource { Test, Camera };

// Estado exibido nas interfaces. Não é o GstState: no cliente, PLAYING da
// pipeline não garante vídeo; o estado vem de probes (pacotes e quadros).
enum class StreamState {
    Stopped,
    Starting,      // pipeline indo para PLAYING
    Transmitting,  // servidor: pacotes saindo pelo udpsink
    Waiting,       // cliente: escutando, nenhum pacote ainda
    Receiving,     // cliente: chegam pacotes, nenhum quadro decodificado
    Playing,       // cliente: quadros decodificados chegando ao sink
    NoSignal,      // cliente: o fluxo parou de chegar
    Error
};

const char* state_name(StreamState s);

struct TransmitterConfig {
    std::string host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    int width = VIDEO_WIDTH;
    int height = VIDEO_HEIGHT;
    int fps = VIDEO_FPS;
    int bitrate_kbps = VIDEO_BITRATE_KBPS;
    std::string preset = VIDEO_PRESET;  // speed-preset do x264enc
    MediaSource source = MediaSource::Test;
};

struct ReceiverConfig {
    int port = DEFAULT_PORT;
};

// Medidas da última janela de ~1 s.
struct VideoStats {
    double fps = 0.0;    // servidor: quadros codificados/s; cliente: quadros exibidos/s
    double kbps = 0.0;   // taxa medida na rede
    uint64_t frames = 0;
    uint64_t bytes = 0;
};

// Validação dos campos; em erro devolve false e preenche 'error'.
bool validate(const TransmitterConfig& cfg, std::string& error);
bool validate(const ReceiverConfig& cfg, std::string& error);
