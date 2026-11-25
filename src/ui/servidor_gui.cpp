// servidor — interface gráfica do transmissor (servidor de mídia), GTK4.
//
// Campos: IP de destino, porta UDP, fonte, resolução, quadros/s, bitrate e
// preset do encoder. Botões Iniciar/Interromper e indicador de estado.
// A pipeline é a do Transmitter (src/core), montada pela API do GStreamer.
//
// Uso: ./servidor [--ip IP] [--porta PORTA]

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>

#include <gtk/gtk.h>

#include "gui_common.hpp"
#include "transmitter.hpp"

namespace {

const char* const RESOLUTIONS[] = {"320 × 240", "640 × 360", "1280 × 720", nullptr};
const int RES_W[] = {320, 640, 1280};
const int RES_H[] = {240, 360, 720};
const char* const PRESETS[] = {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", nullptr};
const char* const SOURCES[] = {"Padrão de teste (bola)", "Câmera", nullptr};

struct App {
    Transmitter tx;
    GuiOptions opts;
    std::string initial_ip = DEFAULT_HOST;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::atomic<unsigned> session{0};
    GtkWidget *window, *ip, *port, *source, *resolution, *fps, *bitrate, *preset, *config_grid;
    GtkWidget *start, *stop, *status, *stats;
};

// Mensagens do núcleo são aplicadas na interface via g_idle_add; 'session'
// descarta mensagens de uma transmissão que já foi interrompida.
enum class Kind { Status, Stats };

struct Msg {
    App* app;
    std::shared_ptr<bool> alive;
    unsigned session;
    Kind kind;
    StreamState state;
    std::string text;
    VideoStats stats;
};

void set_transmitting(App* a, bool on) {
    gtk_widget_set_sensitive(a->config_grid, !on);
    gtk_widget_set_sensitive(a->start, !on);
    gtk_widget_set_sensitive(a->stop, on);
}

void halt(App* a, StreamState state, const std::string& detail = {}) {
    a->session++;
    a->tx.stop();     // GST_STATE_NULL + liberação dos recursos
    set_transmitting(a, false);
    gui_set_status(GTK_LABEL(a->status), state, detail);
}

gboolean apply_msg(gpointer data) {
    std::unique_ptr<Msg> m(static_cast<Msg*>(data));
    App* a = m->app;
    if (!*m->alive || m->session != a->session)
        return G_SOURCE_REMOVE;

    if (m->kind == Kind::Status) {
        if (m->state == StreamState::Error)
            halt(a, StreamState::Error, m->text);
        else
            gui_set_status(GTK_LABEL(a->status), m->state, m->text);
    } else {
        char buf[200];
        std::snprintf(buf, sizeof buf, "Encoder: %s   ·   %.1f quadros/s   ·   %.0f kbps   ·   %s enviados",
                      a->tx.codec().c_str(), m->stats.fps, m->stats.kbps,
                      gui_format_bytes(m->stats.bytes).c_str());
        gtk_label_set_text(GTK_LABEL(a->stats), buf);
    }
    return G_SOURCE_REMOVE;
}

void post(App* a, Kind kind, StreamState st, const std::string& text, const VideoStats& stats = {}) {
    g_idle_add(apply_msg, new Msg{a, a->alive, a->session.load(), kind, st, text, stats});
}

// ---------------------------------------------------------------------------
void iniciar_transmissao(GtkButton*, gpointer data) {
    auto* a = static_cast<App*>(data);
    g_print("Iniciando transmissao...\n");

    // Recupera os valores da interface
    TransmitterConfig cfg;
    cfg.host = gtk_editable_get_text(GTK_EDITABLE(a->ip));
    cfg.port = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->port));
    cfg.fps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->fps));
    cfg.bitrate_kbps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->bitrate));
    cfg.source = gtk_drop_down_get_selected(GTK_DROP_DOWN(a->source)) == 1 ? MediaSource::Camera
                                                                           : MediaSource::Test;
    const guint res = gtk_drop_down_get_selected(GTK_DROP_DOWN(a->resolution));
    cfg.width = RES_W[res];
    cfg.height = RES_H[res];
    cfg.preset = PRESETS[gtk_drop_down_get_selected(GTK_DROP_DOWN(a->preset))];

    // Valida antes de montar a pipeline
    std::string error;
    if (!validate(cfg, error)) {
        gui_set_status(GTK_LABEL(a->status), StreamState::Error, error);
        gtk_widget_grab_focus(a->ip);
        return;
    }

    // Configura e inicia a pipeline
    a->session++;
    gtk_label_set_text(GTK_LABEL(a->stats), "");
    if (a->tx.start(cfg)) {
        set_transmitting(a, true);
        gui_set_status(GTK_LABEL(a->status), StreamState::Starting);
    } else {
        gui_set_status(GTK_LABEL(a->status), StreamState::Error, a->tx.last_error());
    }
}

void interromper_transmissao(GtkButton*, gpointer data) {
    g_print("Interrompendo transmissao...\n");
    halt(static_cast<App*>(data), StreamState::Stopped);
}

// ---------------------------------------------------------------------------
void on_activate(GtkApplication* gapp, gpointer data) {
    auto* a = static_cast<App*>(data);

    a->window = gtk_application_window_new(gapp);
    gtk_window_set_title(GTK_WINDOW(a->window), "Servidor Multimídia — Transmissor");
    gtk_window_set_default_size(GTK_WINDOW(a->window), 540, -1);
    gtk_window_set_resizable(GTK_WINDOW(a->window), FALSE);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_window_set_child(GTK_WINDOW(a->window), box);

    GtkWidget* title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title),
                         "<span size='x-large'><b>Transmissor de vídeo</b></span>\n"
                         "<small>H.264 em MPEG-TS sobre UDP</small>");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(box), title);

    // ---- Configuração ----
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    a->config_grid = grid;

    a->ip = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(a->ip), a->initial_ip.c_str());
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->ip), "ex.: 192.168.0.10");
    gui_grid_row(GTK_GRID(grid), 0, "IP de destino", a->ip);

    a->port = gtk_spin_button_new_with_range(1024, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->port), a->opts.port ? a->opts.port : DEFAULT_PORT);
    gui_grid_row(GTK_GRID(grid), 1, "Porta UDP", a->port);

    a->source = gtk_drop_down_new_from_strings(SOURCES);
    gui_grid_row(GTK_GRID(grid), 2, "Fonte", a->source);

    a->resolution = gtk_drop_down_new_from_strings(RESOLUTIONS);
    for (guint i = 0; RESOLUTIONS[i]; i++)
        if (RES_W[i] == VIDEO_WIDTH && RES_H[i] == VIDEO_HEIGHT)
            gtk_drop_down_set_selected(GTK_DROP_DOWN(a->resolution), i);
    gui_grid_row(GTK_GRID(grid), 3, "Resolução", a->resolution);

    a->fps = gtk_spin_button_new_with_range(1, 60, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->fps), VIDEO_FPS);
    gui_grid_row(GTK_GRID(grid), 4, "Quadros por segundo", a->fps);

    a->bitrate = gtk_spin_button_new_with_range(VIDEO_MIN_BITRATE_KBPS, VIDEO_MAX_BITRATE_KBPS, 100);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->bitrate), VIDEO_BITRATE_KBPS);
    gui_grid_row(GTK_GRID(grid), 5, "Bitrate (kbps)", a->bitrate);

    a->preset = gtk_drop_down_new_from_strings(PRESETS);
    for (guint i = 0; PRESETS[i]; i++)
        if (std::string(PRESETS[i]) == VIDEO_PRESET)
            gtk_drop_down_set_selected(GTK_DROP_DOWN(a->preset), i);
    gtk_widget_set_tooltip_text(a->preset, "speed-preset do x264enc: mais rápido = menos CPU, "
                                           "menos qualidade por bit");
    gui_grid_row(GTK_GRID(grid), 6, "Preset do encoder", a->preset);

    gtk_box_append(GTK_BOX(box), grid);

    // ---- Botões ----
    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    a->start = gtk_button_new_with_label("Iniciar");
    a->stop = gtk_button_new_with_label("Interromper");
    gtk_widget_add_css_class(a->start, "suggested-action");
    gtk_widget_add_css_class(a->stop, "destructive-action");
    gtk_box_append(GTK_BOX(buttons), a->start);
    gtk_box_append(GTK_BOX(buttons), a->stop);
    g_signal_connect(a->start, "clicked", G_CALLBACK(iniciar_transmissao), a);
    g_signal_connect(a->stop, "clicked", G_CALLBACK(interromper_transmissao), a);
    gtk_box_append(GTK_BOX(box), buttons);

    // ---- Estado ----
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    a->status = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(a->status), 0);
    gtk_label_set_wrap(GTK_LABEL(a->status), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(a->status), 60);
    gtk_box_append(GTK_BOX(box), a->status);

    a->stats = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(a->stats), 0);
    gtk_widget_add_css_class(a->stats, "dim-label");
    gtk_box_append(GTK_BOX(box), a->stats);

    set_transmitting(a, false);
    gui_set_status(GTK_LABEL(a->status), StreamState::Stopped);

    a->tx.on_status = [a](StreamState s, const std::string& t) { post(a, Kind::Status, s, t); };
    a->tx.on_stats = [a](const VideoStats& v) { post(a, Kind::Stats, StreamState::Stopped, "", v); };

    gui_schedule_autotest(a->window, a->start, a->stop, a->opts, "servidor", [a] {
        return std::string(gtk_label_get_text(GTK_LABEL(a->status))) + " | " +
               gtk_label_get_text(GTK_LABEL(a->stats));
    });

    gtk_window_present(GTK_WINDOW(a->window));
}

gint on_local_ip(GApplication*, GVariantDict* dict, gpointer data) {
    const char* ip = nullptr;
    if (g_variant_dict_lookup(dict, "ip", "&s", &ip))
        static_cast<App*>(data)->initial_ip = ip;
    return -1;
}

void on_shutdown(GApplication*, gpointer data) {
    auto* a = static_cast<App*>(data);
    *a->alive = false;
    a->tx.stop();   // libera a pipeline ao fechar
}

}  // namespace

int main(int argc, char** argv) {
    gst_init(&argc, &argv);

    App app;
    // NON_UNIQUE: permite abrir mais de um servidor ao mesmo tempo.
    GtkApplication* gapp = gtk_application_new("br.cmp1103.streaming.servidor", G_APPLICATION_NON_UNIQUE);
    gui_add_common_options(gapp, &app.opts);
    g_application_add_main_option(G_APPLICATION(gapp), "ip", 'i', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
                                  "IP de destino inicial", "IP");
    g_signal_connect(gapp, "handle-local-options", G_CALLBACK(on_local_ip), &app);
    g_signal_connect(gapp, "activate", G_CALLBACK(on_activate), &app);
    g_signal_connect(gapp, "shutdown", G_CALLBACK(on_shutdown), &app);
    int rc = g_application_run(G_APPLICATION(gapp), argc, argv);
    g_object_unref(gapp);
    return rc;
}
