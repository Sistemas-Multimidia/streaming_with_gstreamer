#include "config.hpp"

#include <algorithm>
#include <array>

#include <glib.h>

static bool valid_port(int port, std::string& error) {
    if (port < 1 || port > 65535) {
        error = "Porta inválida: " + std::to_string(port) + " (use 1–65535)";
        return false;
    }
    return true;
}

bool validate(const TransmitterConfig& cfg, std::string& error) {
    static const std::array<std::string, 6> presets = {"ultrafast", "superfast", "veryfast",
                                                       "faster",    "fast",      "medium"};

    if (cfg.host.empty() || !g_hostname_is_ip_address(cfg.host.c_str())) {
        error = "IP de destino inválido: \"" + cfg.host + "\"";
        return false;
    }
    if (!valid_port(cfg.port, error))
        return false;
    if (cfg.width < 16 || cfg.height < 16 || cfg.width > 3840 || cfg.height > 2160 || cfg.width % 2 ||
        cfg.height % 2) {
        error = "Resolução inválida: " + std::to_string(cfg.width) + "x" + std::to_string(cfg.height);
        return false;
    }
    if (cfg.fps < 1 || cfg.fps > 60) {
        error = "Taxa de quadros inválida: " + std::to_string(cfg.fps) + " (use 1–60)";
        return false;
    }
    if (cfg.bitrate_kbps < VIDEO_MIN_BITRATE_KBPS || cfg.bitrate_kbps > VIDEO_MAX_BITRATE_KBPS) {
        error = "Bitrate inválido: " + std::to_string(cfg.bitrate_kbps) + " kbps (use " +
                std::to_string(VIDEO_MIN_BITRATE_KBPS) + "–" + std::to_string(VIDEO_MAX_BITRATE_KBPS) + ")";
        return false;
    }
    if (std::find(presets.begin(), presets.end(), cfg.preset) == presets.end()) {
        error = "Preset desconhecido: \"" + cfg.preset + "\"";
        return false;
    }
    return true;
}

bool validate(const ReceiverConfig& cfg, std::string& error) { return valid_port(cfg.port, error); }
