#include "gui_common.hpp"

#include <cstdio>

static gint on_local_options(GApplication*, GVariantDict* dict, gpointer data) {
    auto* o = static_cast<GuiOptions*>(data);
    const char* cap = nullptr;
    g_variant_dict_lookup(dict, "porta", "i", &o->port);
    g_variant_dict_lookup(dict, "autoteste", "i", &o->autotest_s);
    if (g_variant_dict_lookup(dict, "captura", "&s", &cap))
        o->capture = cap;
    return -1;   // continua a execução normal
}

void gui_add_common_options(GtkApplication* app, GuiOptions* opts) {
    g_application_add_main_option(G_APPLICATION(app), "porta", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_INT,
                                  "Porta UDP inicial", "PORTA");
    g_application_add_main_option(G_APPLICATION(app), "autoteste", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT,
                                  "Teste automático: clica Iniciar, espera N s, imprime o estado e fecha",
                                  "N");
    g_application_add_main_option(G_APPLICATION(app), "captura", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
                                  "No autoteste, salva a janela neste PNG", "ARQUIVO.png");
    g_signal_connect(app, "handle-local-options", G_CALLBACK(on_local_options), opts);
}

void gui_set_status(GtkLabel* label, StreamState state, const std::string& detail) {
    const char* color;
    switch (state) {
    case StreamState::Transmitting:
    case StreamState::Playing:   color = "#2e9e44"; break;   // verde
    case StreamState::Starting:
    case StreamState::Waiting:
    case StreamState::Receiving: color = "#d08a00"; break;   // âmbar
    case StreamState::NoSignal:
    case StreamState::Error:     color = "#c62828"; break;   // vermelho
    default:                     color = "#808080"; break;   // cinza
    }
    gchar* markup = g_markup_printf_escaped("<span foreground='%s' size='large'>●</span>  <b>%s</b>%s%s", color,
                                            state_name(state), detail.empty() ? "" : " — ", detail.c_str());
    gtk_label_set_markup(label, markup);
    g_free(markup);
}

std::string gui_format_bytes(uint64_t b) {
    char buf[32];
    if (b < 1024)
        std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)b);
    else if (b < 1024 * 1024)
        std::snprintf(buf, sizeof buf, "%.0f KB", b / 1024.0);
    else
        std::snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024.0));
    return buf;
}

void gui_grid_row(GtkGrid* grid, int row, const char* text, GtkWidget* widget) {
    GtkWidget* l = gtk_label_new(text);
    gtk_widget_set_halign(l, GTK_ALIGN_END);
    gtk_grid_attach(grid, l, 0, row, 1, 1);
    gtk_widget_set_hexpand(widget, TRUE);
    gtk_grid_attach(grid, widget, 1, row, 1, 1);
}

bool gui_save_screenshot(GtkWidget* window, const std::string& path) {
    const int w = gtk_widget_get_width(window);
    const int h = gtk_widget_get_height(window);
    if (w <= 1 || h <= 1)
        return false;

    GdkPaintable* paintable = gtk_widget_paintable_new(window);
    GtkSnapshot* snap = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, snap, w, h);
    GskRenderNode* node = gtk_snapshot_free_to_node(snap);
    g_object_unref(paintable);
    if (!node)
        return false;

    GskRenderer* renderer = gtk_native_get_renderer(gtk_widget_get_native(window));
    graphene_rect_t area = GRAPHENE_RECT_INIT(0, 0, float(w), float(h));
    GdkTexture* tex = gsk_renderer_render_texture(renderer, node, &area);
    const bool ok = gdk_texture_save_to_png(tex, path.c_str());
    g_object_unref(tex);
    gsk_render_node_unref(node);
    return ok;
}

namespace {
struct AutoTest {
    GtkWidget *window, *start, *stop;
    GuiOptions opts;
    std::string name;
    std::function<std::string()> report;
};

gboolean autotest_finish(gpointer data) {
    auto* t = static_cast<AutoTest*>(data);
    std::printf("AUTOTESTE %s: %s\n", t->name.c_str(), t->report().c_str());
    if (!t->opts.capture.empty())
        std::printf("AUTOTESTE captura %s: %s\n", t->opts.capture.c_str(),
                    gui_save_screenshot(t->window, t->opts.capture) ? "ok" : "falhou");
    std::fflush(stdout);
    g_signal_emit_by_name(t->stop, "clicked");
    gtk_window_destroy(GTK_WINDOW(t->window));
    delete t;
    return G_SOURCE_REMOVE;
}

gboolean autotest_begin(gpointer data) {
    auto* t = static_cast<AutoTest*>(data);
    g_signal_emit_by_name(t->start, "clicked");
    g_timeout_add_seconds(guint(t->opts.autotest_s), autotest_finish, t);
    return G_SOURCE_REMOVE;
}
}  // namespace

void gui_schedule_autotest(GtkWidget* window, GtkWidget* start, GtkWidget* stop, const GuiOptions& opts,
                           const char* name, std::function<std::string()> report) {
    if (opts.autotest_s <= 0)
        return;
    // Espera a janela ser mapeada antes de clicar.
    g_timeout_add(500, autotest_begin, new AutoTest{window, start, stop, opts, name, std::move(report)});
}
