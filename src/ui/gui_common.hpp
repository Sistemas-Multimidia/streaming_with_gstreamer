#pragma once

// Utilitários GTK4 compartilhados por servidor_gui.cpp e cliente_gui.cpp.

#include <functional>
#include <string>

#include <gtk/gtk.h>

#include "config.hpp"

// Opções de linha de comando comuns (lidas em "handle-local-options").
struct GuiOptions {
    int port = 0;            // 0 = padrão
    int autotest_s = 0;      // >0: clica Iniciar, espera N s, imprime o estado e fecha
    std::string capture;     // PNG da janela no fim do autoteste
};

// Registra --porta, --autoteste e --captura na GtkApplication.
void gui_add_common_options(GtkApplication* app, GuiOptions* opts);

// Indicador de estado: bolinha colorida + nome do estado + detalhe.
void gui_set_status(GtkLabel* label, StreamState state, const std::string& detail = {});

// "123 KB", "1.2 MB".
std::string gui_format_bytes(uint64_t bytes);

// Linha "rótulo | widget" numa GtkGrid.
void gui_grid_row(GtkGrid* grid, int row, const char* label, GtkWidget* widget);

// Renderiza a janela num PNG (funciona em X11 e Wayland).
bool gui_save_screenshot(GtkWidget* window, const std::string& path);

// Autoteste: clica 'start', espera 'seconds', imprime report(), salva a
// captura (se pedida), clica 'stop' e fecha a janela.
void gui_schedule_autotest(GtkWidget* window, GtkWidget* start, GtkWidget* stop, const GuiOptions& opts,
                           const char* name, std::function<std::string()> report);
