// cliente — interface gráfica do receptor (cliente de mídia), GTK4.
//
// Campos: porta UDP e modo de exibição. Área de reprodução, botões
// Iniciar/Interromper e indicador de estado.
//
// Exibição:
//  - "Nesta janela" (desafio adicional): gtk4paintablesink entrega um
//    GdkPaintable que é mostrado num GtkPicture. Funciona em X11 e Wayland.
//  - "Janela separada": autovideosink, como pede a versão inicial do PDF.
//
// O indicador NÃO usa o estado PLAYING da pipeline como prova de recepção:
// ele vem do Receiver, que conta pacotes UDP e quadros decodificados.
//
// Uso: ./cliente [--porta PORTA] [--janela-separada]

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>

#include <gtk/gtk.h>

#include "gui_common.hpp"
#include "receiver.hpp"

namespace {

const char* const DISPLAY_MODES[] = {"Nesta janela (gtk4paintablesink)", "Janela separada (autovideosink)",
                                     nullptr};

struct App {
    Receiver rx;
    GuiOptions opts;
    bool separate_window = false;
    bool has_paintable_sink = false;
    bool embedded = false;   // modo da recepção atual
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::atomic<unsigned> session{0};
    GtkWidget *window, *port, *display, *config_grid;
    GtkWidget *video_stack, *video_picture, *video_notice;
    GtkWidget *start, *stop, *status, *stats;
};

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

void show_notice(App* a, const char* text) {
    gchar* m = g_markup_printf_escaped("<span size='large' foreground='#9a9a9a'>%s</span>", text);
    gtk_label_set_markup(GTK_LABEL(a->video_notice), m);
    g_free(m);
    gtk_stack_set_visible_child_name(GTK_STACK(a->video_stack), "aviso");
}

void set_receiving(App* a, bool on) {
    gtk_widget_set_sensitive(a->config_grid, !on);
    gtk_widget_set_sensitive(a->start, !on);
    gtk_widget_set_sensitive(a->stop, on);
}

void halt(App* a, StreamState state, const std::string& detail, const char* notice) {
    a->session++;
    a->rx.stop();   // GST_STATE_NULL + liberação dos recursos
    gtk_picture_set_paintable(GTK_PICTURE(a->video_picture), nullptr);
    show_notice(a, notice);
    set_receiving(a, false);
    gui_set_status(GTK_LABEL(a->status), state, detail);
}

void update_video_area(App* a, StreamState s) {
    if (!a->embedded)
        return;
    switch (s) {
    case StreamState::Playing:
        gtk_stack_set_visible_child_name(GTK_STACK(a->video_stack), "video");
        break;
    case StreamState::Waiting:   show_notice(a, "Aguardando o transmissor…"); break;
    case StreamState::Receiving: show_notice(a, "Recebendo pacotes, aguardando quadro-chave…"); break;
    case StreamState::NoSignal:  show_notice(a, "Sem sinal — o transmissor parou?"); break;
    default: break;
    }
}

gboolean apply_msg(gpointer data) {
    std::unique_ptr<Msg> m(static_cast<Msg*>(data));
    App* a = m->app;
    if (!*m->alive || m->session != a->session)
        return G_SOURCE_REMOVE;

    if (m->kind == Kind::Status) {
        if (m->state == StreamState::Error) {
            halt(a, StreamState::Error, m->text, "Erro na recepção");
        } else {
            gui_set_status(GTK_LABEL(a->status), m->state, m->text);
            update_video_area(a, m->state);
        }
    } else {
        char buf[200];
        std::snprintf(buf, sizeof buf, "Decoder: %s   ·   %.1f quadros/s   ·   %.0f kbps   ·   %s recebidos",
                      a->rx.codec().c_str(), m->stats.fps, m->stats.kbps,
                      gui_format_bytes(m->stats.bytes).c_str());
        gtk_label_set_text(GTK_LABEL(a->stats), buf);
    }
    return G_SOURCE_REMOVE;
}

void post(App* a, Kind kind, StreamState st, const std::string& text, const VideoStats& stats = {}) {
    g_idle_add(apply_msg, new Msg{a, a->alive, a->session.load(), kind, st, text, stats});
}

// ---------------------------------------------------------------------------
void iniciar_recepcao(GtkButton*, gpointer data) {
    auto* a = static_cast<App*>(data);
    g_print("Iniciando recepcao...\n");

    ReceiverConfig cfg;
    cfg.port = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->port));
    std::string error;
    if (!validate(cfg, error)) {
        gui_set_status(GTK_LABEL(a->status), StreamState::Error, error);
        return;
    }

    a->embedded = a->has_paintable_sink && gtk_drop_down_get_selected(GTK_DROP_DOWN(a->display)) == 0;
    GstElement* sink = nullptr;
    if (a->embedded) {
        // O sink fornece um GdkPaintable; o GtkPicture o desenha na janela.
        sink = gst_element_factory_make("gtk4paintablesink", "tela");
        GdkPaintable* paintable = nullptr;
        g_object_get(sink, "paintable", &paintable, nullptr);
        gtk_picture_set_paintable(GTK_PICTURE(a->video_picture), paintable);
        g_object_unref(paintable);
    }

    a->session++;
    gtk_label_set_text(GTK_LABEL(a->stats), "");
    if (a->rx.start(cfg, sink)) {
        set_receiving(a, true);
        gui_set_status(GTK_LABEL(a->status), StreamState::Starting);
        show_notice(a, a->embedded ? "Aguardando o transmissor…" : "O vídeo abre em uma janela separada");
    } else {
        gtk_picture_set_paintable(GTK_PICTURE(a->video_picture), nullptr);
        gui_set_status(GTK_LABEL(a->status), StreamState::Error, a->rx.last_error());
    }
}

void interromper_recepcao(GtkButton*, gpointer data) {
    g_print("Interrompendo recepcao...\n");
    halt(static_cast<App*>(data), StreamState::Stopped, {}, "Sem vídeo");
}

// ---------------------------------------------------------------------------
void on_activate(GtkApplication* gapp, gpointer data) {
    auto* a = static_cast<App*>(data);

    if (GstElementFactory* f = gst_element_factory_find("gtk4paintablesink")) {
        a->has_paintable_sink = true;
        gst_object_unref(f);
    }

    a->window = gtk_application_window_new(gapp);
    gtk_window_set_title(GTK_WINDOW(a->window), "Cliente Multimídia — Receptor");
    gtk_window_set_default_size(GTK_WINDOW(a->window), 720, 620);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_window_set_child(GTK_WINDOW(a->window), box);

    GtkWidget* title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title),
                         "<span size='x-large'><b>Receptor de vídeo</b></span>\n"
                         "<small>H.264 em MPEG-TS sobre UDP</small>");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(box), title);

    // ---- Configuração + botões ----
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_hexpand(grid, TRUE);
    a->config_grid = grid;

    a->port = gtk_spin_button_new_with_range(1024, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->port), a->opts.port ? a->opts.port : DEFAULT_PORT);
    gui_grid_row(GTK_GRID(grid), 0, "Porta UDP", a->port);

    a->display = gtk_drop_down_new_from_strings(DISPLAY_MODES);
    if (!a->has_paintable_sink || a->separate_window)
        gtk_drop_down_set_selected(GTK_DROP_DOWN(a->display), 1);
    if (!a->has_paintable_sink) {
        gtk_widget_set_sensitive(a->display, FALSE);
        gtk_widget_set_tooltip_text(a->display, "gtk4paintablesink não instalado "
                                                "(pacote gstreamer1-plugin-gtk4)");
    }
    gui_grid_row(GTK_GRID(grid), 1, "Exibição", a->display);
    gtk_box_append(GTK_BOX(bar), grid);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    a->start = gtk_button_new_with_label("Iniciar");
    a->stop = gtk_button_new_with_label("Interromper");
    gtk_widget_add_css_class(a->start, "suggested-action");
    gtk_widget_add_css_class(a->stop, "destructive-action");
    gtk_box_append(GTK_BOX(buttons), a->start);
    gtk_box_append(GTK_BOX(buttons), a->stop);
    g_signal_connect(a->start, "clicked", G_CALLBACK(iniciar_recepcao), a);
    g_signal_connect(a->stop, "clicked", G_CALLBACK(interromper_recepcao), a);
    gtk_box_append(GTK_BOX(bar), buttons);
    gtk_box_append(GTK_BOX(box), bar);

    // ---- Área de reprodução ----
    GtkWidget* frame = gtk_frame_new(nullptr);
    gtk_widget_set_size_request(frame, 480, 270);
    gtk_widget_set_vexpand(frame, TRUE);
    a->video_stack = gtk_stack_new();
    a->video_notice = gtk_label_new(nullptr);
    a->video_picture = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(a->video_picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_stack_add_named(GTK_STACK(a->video_stack), a->video_notice, "aviso");
    gtk_stack_add_named(GTK_STACK(a->video_stack), a->video_picture, "video");
    gtk_frame_set_child(GTK_FRAME(frame), a->video_stack);
    gtk_box_append(GTK_BOX(box), frame);
    show_notice(a, "Sem vídeo");

    // ---- Estado ----
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    a->status = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(a->status), 0);
    gtk_label_set_wrap(GTK_LABEL(a->status), TRUE);
    gtk_box_append(GTK_BOX(box), a->status);

    a->stats = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(a->stats), 0);
    gtk_widget_add_css_class(a->stats, "dim-label");
    gtk_box_append(GTK_BOX(box), a->stats);

    set_receiving(a, false);
    gui_set_status(GTK_LABEL(a->status), StreamState::Stopped);

    a->rx.on_status = [a](StreamState s, const std::string& t) { post(a, Kind::Status, s, t); };
    a->rx.on_stats = [a](const VideoStats& v) { post(a, Kind::Stats, StreamState::Stopped, "", v); };

    gui_schedule_autotest(a->window, a->start, a->stop, a->opts, "cliente", [a] {
        return std::string(gtk_label_get_text(GTK_LABEL(a->status))) + " | " +
               gtk_label_get_text(GTK_LABEL(a->stats));
    });

    gtk_window_present(GTK_WINDOW(a->window));
}

gint on_local_display(GApplication*, GVariantDict* dict, gpointer data) {
    if (g_variant_dict_contains(dict, "janela-separada"))
        static_cast<App*>(data)->separate_window = true;
    return -1;
}

void on_shutdown(GApplication*, gpointer data) {
    auto* a = static_cast<App*>(data);
    *a->alive = false;
    a->rx.stop();
}

}  // namespace

int main(int argc, char** argv) {
    // O renderizador Vulkan do GTK 4.22 travou (SIGSEGV em
    // gsk_vulkan_direct_allocator_alloc) ao enviar quadros de vídeo com o
    // driver NVIDIA 620 no Linux. O renderizador OpenGL ("ngl") funciona; usamos ele
    // por padrão, mas respeitamos GSK_RENDERER se o usuário definir outro.
#ifndef G_OS_WIN32
    g_setenv("GSK_RENDERER", "ngl", FALSE);
#endif

    gst_init(&argc, &argv);

    App app;
    GtkApplication* gapp = gtk_application_new("br.cmp1103.streaming.cliente", G_APPLICATION_NON_UNIQUE);
    gui_add_common_options(gapp, &app.opts);
    g_application_add_main_option(G_APPLICATION(gapp), "janela-separada", 's', G_OPTION_FLAG_NONE,
                                  G_OPTION_ARG_NONE, "Começa com o vídeo em janela separada (autovideosink)",
                                  nullptr);
    g_signal_connect(gapp, "handle-local-options", G_CALLBACK(on_local_display), &app);
    g_signal_connect(gapp, "activate", G_CALLBACK(on_activate), &app);
    g_signal_connect(gapp, "shutdown", G_CALLBACK(on_shutdown), &app);
    int rc = g_application_run(G_APPLICATION(gapp), argc, argv);
    g_object_unref(gapp);
    return rc;
}
