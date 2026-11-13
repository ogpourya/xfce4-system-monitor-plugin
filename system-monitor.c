// SPDX-License-Identifier: MIT
#include "sysmon.h"
#include <libxfce4util/libxfce4util.h>
#include <string.h>

const MetricMeta METRIC_META[METRIC_COUNT] = {
    [METRIC_CPU]  = { "show_cpu",  "cpu_icon",  "builtin-cpu",  "cpu_color",  "sysmon-cpu",  "Show CPU" },
    [METRIC_RAM]  = { "show_ram",  "ram_icon",  "builtin-ram",  "ram_color",  "sysmon-ram",  "Show RAM" },
    [METRIC_NET]  = { "show_net",  NULL,        NULL,           NULL,         NULL,          "Show Network" },
    [METRIC_DISK] = { "show_disk", "disk_icon", "builtin-disk", "disk_color", "sysmon-disk", "Show Disk" },
};

static const char *const BUILTIN_SVGS[] = {
    [BUILTIN_CPU] =
        "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
        "<path fill='#2e3436' d='M5 1v2H4c-.55 0-1 .45-1 1v1H1v1.5h2v1H1V9h2v1H1v1.5h2v1c0 .55.45 1 1 1h1v2h1.5v-2h1v2H9v-2h1v2h1.5v-2h1c.55 0 1-.45 1-1v-1h2V10h-2V9h2V7.5h-2v-1h2V5h-2V4c0-.55-.45-1-1-1h-1V1H10v2H9V1H7.5v2h-1V1H5zm0 4h6v6H5V5zm1.5 1.5v3h3v-3h-3z'/>"
        "</svg>",
    [BUILTIN_RAM] =
        "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
        "<path fill='#2e3436' d='M2 3c-.55 0-1 .45-1 1v2.2a1.3 1.3 0 0 1 0 2.6V11c0 .55.45 1 1 1h4.5v-1.5h1V12H14c.55 0 1-.45 1-1V8.8a1.3 1.3 0 0 1 0-2.6V4c0-.55-.45-1-1-1H2zm1.5 2h2v4h-2V5zm3.5 0h2v4H7V5zm3.5 0h2v4h-2V5zM2 13v1h2v-1H2zm3 0v1h2v-1H5zm4 0v1h2v-1H9zm3 0v1h2v-1h-2z'/>"
        "</svg>",
    [BUILTIN_DISK] =
        "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
        "<path fill='#2e3436' d='M3.5 1C2.67 1 2 1.67 2 2.5v11c0 .83.67 1.5 1.5 1.5h9c.83 0 1.5-.67 1.5-1.5v-11c0-.83-.67-1.5-1.5-1.5h-9zM8 3a3.5 3.5 0 1 1 0 7 3.5 3.5 0 0 1 0-7zm0 2a1.5 1.5 0 1 0 0 3 1.5 1.5 0 0 0 0-3zm-4 7h8v1.5H4V12z'/>"
        "</svg>",
};

void sysmon_parse_icon_spec(IconSpec *spec, const char *raw, BuiltinIcon fallback) {
    (void)fallback;
    const char *s = raw ? raw : "";
    if (g_strcmp0(s, "xsi-cpu-symbolic") == 0) s = "builtin-cpu";
    if (g_strcmp0(s, "xsi-ram-symbolic") == 0 || g_strcmp0(s, "media-flash-symbolic") == 0) s = "builtin-ram";

    g_free(spec->name);
    spec->name = g_strdup(s);

    if (*s == '\0') {
        spec->kind = ICON_NONE;
    } else if (strcmp(s, "builtin-cpu") == 0) {
        spec->kind = ICON_BUILTIN;
        spec->builtin = BUILTIN_CPU;
    } else if (strcmp(s, "builtin-ram") == 0) {
        spec->kind = ICON_BUILTIN;
        spec->builtin = BUILTIN_RAM;
    } else if (strcmp(s, "builtin-disk") == 0) {
        spec->kind = ICON_BUILTIN;
        spec->builtin = BUILTIN_DISK;
    } else {
        spec->kind = ICON_THEME;
    }
}

static void load_config(SysMon *m) {
    for (int i = 0; i < METRIC_COUNT; i++) {
        m->metrics[i].enabled = TRUE;
        m->metrics[i].color = g_strdup("");
        if (METRIC_META[i].default_icon)
            sysmon_parse_icon_spec(&m->metrics[i].icon, METRIC_META[i].default_icon, (BuiltinIcon)i);
    }
    m->show_icons = TRUE;
    m->click_to_cycle = TRUE;
    m->cycle_state = 3;
    m->net_iface = g_strdup("auto");
    m->disk_path = g_strdup("/");
    m->interval_ms = 250;
    m->icon_size = 13;
    m->font_size = 10;
    m->net_rx_color = g_strdup("");
    m->net_tx_color = g_strdup("");
    m->dot_color = g_strdup("#999999");

    g_autofree gchar *file = xfce_panel_plugin_lookup_rc_file(m->plugin);
    if (!file) return;
    XfceRc *rc = xfce_rc_simple_open(file, TRUE);
    if (!rc) return;

    for (int i = 0; i < METRIC_COUNT; i++) {
        const MetricMeta *meta = &METRIC_META[i];
        m->metrics[i].enabled = xfce_rc_read_bool_entry(rc, meta->show_key, TRUE);
        if (meta->icon_key) {
            const char *val = xfce_rc_read_entry(rc, meta->icon_key, meta->default_icon);
            sysmon_parse_icon_spec(&m->metrics[i].icon, val, (BuiltinIcon)i);
        }
        if (meta->color_key) {
            g_free(m->metrics[i].color);
            m->metrics[i].color = g_strdup(xfce_rc_read_entry(rc, meta->color_key, ""));
        }
    }

    m->show_icons = xfce_rc_read_bool_entry(rc, "show_icons", TRUE);
    m->click_to_cycle = xfce_rc_read_bool_entry(rc, "click_to_cycle", TRUE);
    m->cycle_state = CLAMP(xfce_rc_read_int_entry(rc, "cycle_state", 3), 0, 3);

    g_free(m->net_iface);
    m->net_iface = g_strdup(xfce_rc_read_entry(rc, "net_iface", "auto"));
    g_free(m->disk_path);
    m->disk_path = g_strdup(xfce_rc_read_entry(rc, "disk_path", "/"));

    m->interval_ms = CLAMP(xfce_rc_read_int_entry(rc, "interval_ms", 250), 100, 10000);
    m->icon_size = CLAMP(xfce_rc_read_int_entry(rc, "icon_size", 13), 8, 32);
    m->font_size = CLAMP(xfce_rc_read_int_entry(rc, "font_size", 10), 7, 24);

    g_free(m->net_rx_color); m->net_rx_color = g_strdup(xfce_rc_read_entry(rc, "net_rx_color", ""));
    g_free(m->net_tx_color); m->net_tx_color = g_strdup(xfce_rc_read_entry(rc, "net_tx_color", ""));
    g_free(m->dot_color);    m->dot_color    = g_strdup(xfce_rc_read_entry(rc, "dot_color", "#999999"));

    xfce_rc_close(rc);
}

static void save_config_now(SysMon *m) {
    g_autofree gchar *file = xfce_panel_plugin_save_location(m->plugin, TRUE);
    if (!file) return;
    XfceRc *rc = xfce_rc_simple_open(file, FALSE);
    if (!rc) return;

    for (int i = 0; i < METRIC_COUNT; i++) {
        const MetricMeta *meta = &METRIC_META[i];
        xfce_rc_write_bool_entry(rc, meta->show_key, m->metrics[i].enabled);
        if (meta->icon_key)
            xfce_rc_write_entry(rc, meta->icon_key, m->metrics[i].icon.name ? m->metrics[i].icon.name : "");
        if (meta->color_key)
            xfce_rc_write_entry(rc, meta->color_key, m->metrics[i].color ? m->metrics[i].color : "");
    }

    xfce_rc_write_bool_entry(rc, "show_icons", m->show_icons);
    xfce_rc_write_bool_entry(rc, "click_to_cycle", m->click_to_cycle);
    xfce_rc_write_int_entry(rc, "cycle_state", m->cycle_state);

    xfce_rc_write_entry(rc, "net_iface", m->net_iface);
    xfce_rc_write_entry(rc, "disk_path", m->disk_path);
    xfce_rc_write_int_entry(rc, "interval_ms", m->interval_ms);
    xfce_rc_write_int_entry(rc, "icon_size", m->icon_size);
    xfce_rc_write_int_entry(rc, "font_size", m->font_size);

    xfce_rc_write_entry(rc, "net_rx_color", m->net_rx_color);
    xfce_rc_write_entry(rc, "net_tx_color", m->net_tx_color);
    xfce_rc_write_entry(rc, "dot_color", m->dot_color);

    xfce_rc_close(rc);
}

static gboolean on_save_timeout(gpointer user_data) {
    SysMon *m = user_data;
    m->save_timeout_id = 0;
    save_config_now(m);
    return G_SOURCE_REMOVE;
}

void sysmon_schedule_save(SysMon *m) {
    if (m->save_timeout_id) g_source_remove(m->save_timeout_id);
    m->save_timeout_id = g_timeout_add(300, on_save_timeout, m);
}

void sysmon_flush_save(SysMon *m) {
    if (m->save_timeout_id) {
        g_source_remove(m->save_timeout_id);
        m->save_timeout_id = 0;
        save_config_now(m);
    }
}

void sysmon_apply_css(SysMon *m) {
    if (!m->css) {
        m->css = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(m->css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    GString *s = g_string_sized_new(256);
    g_string_append(s, ".sysmon-box { padding: 1px 5px; }\n");
    g_string_append_printf(s, ".sysmon-val { font-size: %dpx; font-weight: bold; min-width: 32px; }\n", m->font_size);
    g_string_append_printf(s, ".sysmon-net { font-size: %dpx; font-weight: bold; }\n", m->font_size);
    if (m->dot_color && *m->dot_color)
        g_string_append_printf(s, ".sysmon-dot { font-size: %dpx; color: %s; }\n", MAX(7, m->font_size - 1), m->dot_color);
    else
        g_string_append_printf(s, ".sysmon-dot { font-size: %dpx; }\n", MAX(7, m->font_size - 1));

    for (int i = 0; i < METRIC_COUNT; i++) {
        if (METRIC_META[i].css_class && m->metrics[i].color && *m->metrics[i].color)
            g_string_append_printf(s, ".%s { color: %s; }\n", METRIC_META[i].css_class, m->metrics[i].color);
    }
    if (m->net_rx_color && *m->net_rx_color)
        g_string_append_printf(s, ".sysmon-net-rx { color: %s; }\n", m->net_rx_color);
    if (m->net_tx_color && *m->net_tx_color)
        g_string_append_printf(s, ".sysmon-net-tx { color: %s; }\n", m->net_tx_color);

    gtk_css_provider_load_from_data(m->css, s->str, (gssize)s->len, NULL);
    g_string_free(s, TRUE);
}

int sysmon_enabled_count(const SysMon *m) {
    int count = 0;
    for (int i = 0; i < METRIC_COUNT; i++)
        if (m->metrics[i].enabled) count++;
    return count;
}

static gboolean effective_show(const SysMon *m, int idx) {
    if (!m->metrics[idx].enabled) return FALSE;
    if (!m->click_to_cycle) return TRUE;
    int rank = 0;
    for (int i = 0; i < idx; i++)
        if (m->metrics[i].enabled) rank++;
    return rank <= m->cycle_state;
}

static GtkWidget *make_label(const char *text, const char *c1, const char *c2) {
    GtkWidget *lbl = gtk_label_new(text);
    GtkStyleContext *sc = gtk_widget_get_style_context(lbl);
    if (c1) gtk_style_context_add_class(sc, c1);
    if (c2) gtk_style_context_add_class(sc, c2);
    return lbl;
}

static GtkWidget *make_builtin_image(BuiltinIcon id, int size, const char *custom_hex, GtkStyleContext *ctx) {
    GdkRGBA fg;
    if (!(custom_hex && *custom_hex && gdk_rgba_parse(&fg, custom_hex)))
        gtk_style_context_get_color(ctx, GTK_STATE_FLAG_NORMAL, &fg);

    char hex[8];
    g_snprintf(hex, sizeof(hex), "#%02x%02x%02x",
        (int)(fg.red * 255.0 + 0.5),
        (int)(fg.green * 255.0 + 0.5),
        (int)(fg.blue * 255.0 + 0.5));

    g_autofree gchar *tinted = g_strdup(BUILTIN_SVGS[id]);
    char *pos = strstr(tinted, "#2e3436");
    if (pos) memcpy(pos, hex, 7);

    g_autoptr(GInputStream) stream = g_memory_input_stream_new_from_data(tinted, -1, NULL);
    g_autoptr(GdkPixbuf) pb = gdk_pixbuf_new_from_stream_at_scale(stream, size, size, TRUE, NULL, NULL);
    return pb ? gtk_image_new_from_pixbuf(pb) : gtk_image_new();
}

static GtkWidget *make_scalar_item(SysMon *m, const MetricSlot *slot, const char *css_class) {
    GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    if (css_class)
        gtk_style_context_add_class(gtk_widget_get_style_context(hb), css_class);

    if (m->show_icons && slot->icon.kind != ICON_NONE) {
        GtkWidget *img = NULL;
        if (slot->icon.kind == ICON_BUILTIN) {
            img = make_builtin_image(slot->icon.builtin, m->icon_size, slot->color, gtk_widget_get_style_context(m->box));
        } else {
            img = gtk_image_new_from_icon_name(slot->icon.name, GTK_ICON_SIZE_MENU);
            gtk_image_set_pixel_size(GTK_IMAGE(img), m->icon_size);
        }
        gtk_box_pack_start(GTK_BOX(hb), img, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(hb), slot->label, FALSE, FALSE, 0);
    return hb;
}

void sysmon_rebuild_ui(SysMon *m) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(m->box));
    for (GList *l = children; l != NULL; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    for (int i = 0; i < METRIC_COUNT; i++) {
        m->metrics[i].label = NULL;
        m->metrics[i].disp_pct = -1;
    }
    m->net_rx_label = m->net_tx_label = NULL;
    m->disp_net_rx[0] = m->disp_net_tx[0] = '\0';

    gboolean any = FALSE;
    char buf[16];

    for (int i = 0; i < METRIC_COUNT; i++) {
        if (!effective_show(m, i)) continue;
        if (any)
            gtk_box_pack_start(GTK_BOX(m->box), make_label(" · ", "sysmon-dot", NULL), FALSE, FALSE, 0);

        if (i == METRIC_NET) {
            GtkWidget *nb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_widget_set_margin_start(nb, 4);
            gtk_widget_set_margin_end(nb, 4);
            g_strlcpy(m->disp_net_rx, m->net_rx_text, sizeof(m->disp_net_rx));
            g_strlcpy(m->disp_net_tx, m->net_tx_text, sizeof(m->disp_net_tx));
            m->net_rx_label = make_label(m->net_rx_text, "sysmon-net", "sysmon-net-rx");
            m->net_tx_label = make_label(m->net_tx_text, "sysmon-net", "sysmon-net-tx");
            gtk_box_pack_start(GTK_BOX(nb), m->net_rx_label, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(nb), make_label("  │  ", "sysmon-dot", NULL), FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(nb), m->net_tx_label, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(m->box), nb, FALSE, FALSE, 0);
        } else {
            MetricSlot *s = &m->metrics[i];
            s->disp_pct = s->pct;
            g_snprintf(buf, sizeof(buf), "%d%%", s->pct);
            s->label = make_label(buf, "sysmon-val", METRIC_META[i].css_class);
            gtk_box_pack_start(GTK_BOX(m->box), make_scalar_item(m, s, METRIC_META[i].css_class), FALSE, FALSE, 0);
        }
        any = TRUE;
    }

    if (!any)
        gtk_box_pack_start(GTK_BOX(m->box), make_label("SysMon", "sysmon-val", NULL), FALSE, FALSE, 0);

    gtk_widget_show_all(m->box);
}

void sysmon_restart_timer(SysMon *m) {
    if (m->timeout_id) g_source_remove(m->timeout_id);
    m->timeout_id = g_timeout_add((guint)m->interval_ms, metrics_update, m);
}

static gboolean on_button_press(GtkWidget *w, GdkEventButton *ev, gpointer user_data) {
    (void)w;
    SysMon *m = user_data;
    if (ev->type == GDK_BUTTON_PRESS && ev->button == 1 && m->click_to_cycle) {
        int n = sysmon_enabled_count(m);
        if (n > 1) {
            m->cycle_state = (m->cycle_state + 1) % n;
            sysmon_rebuild_ui(m);
            save_config_now(m);
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean on_query_tooltip(GtkWidget *w, gint x, gint y, gboolean kb, GtkTooltip *tip, gpointer user_data) {
    (void)w; (void)x; (void)y; (void)kb;
    SysMon *m = user_data;
    const char *iface = "auto (none)";
    if (!m->net_iface || strcmp(m->net_iface, "auto") == 0) {
        if (m->cached_auto_iface[0]) iface = m->cached_auto_iface;
    } else {
        iface = m->net_iface;
    }
    g_autofree gchar *txt = g_strdup_printf(
        "CPU: %d%%  |  RAM: %d%%\nNet (%s): %s  %s\nDisk (%s): %d%%",
        m->metrics[METRIC_CPU].pct, m->metrics[METRIC_RAM].pct,
        iface, m->net_rx_text, m->net_tx_text,
        m->disk_path, m->metrics[METRIC_DISK].pct);
    gtk_tooltip_set_text(tip, txt);
    return TRUE;
}

static gboolean on_size_changed(XfcePanelPlugin *plugin, gint size, gpointer user_data) {
    (void)user_data;
    if (xfce_panel_plugin_get_orientation(plugin) == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_set_size_request(GTK_WIDGET(plugin), -1, size);
    else
        gtk_widget_set_size_request(GTK_WIDGET(plugin), size, -1);
    return TRUE;
}

static void on_free_data(XfcePanelPlugin *plugin, gpointer user_data) {
    (void)plugin;
    SysMon *m = user_data;
    if (m->timeout_id) g_source_remove(m->timeout_id);
    sysmon_flush_save(m);
    metrics_close_fds(m);
    if (m->css) g_object_unref(m->css);

    for (int i = 0; i < METRIC_COUNT; i++) {
        g_free(m->metrics[i].icon.name);
        g_free(m->metrics[i].color);
    }
    g_free(m->net_iface);
    g_free(m->disk_path);
    g_free(m->net_rx_color);
    g_free(m->net_tx_color);
    g_free(m->dot_color);
    g_free(m);
}

static void sysmon_construct(XfcePanelPlugin *plugin) {
    SysMon *m = g_new0(SysMon, 1);
    m->plugin = plugin;
    metrics_init_fds(m);

    g_strlcpy(m->net_rx_text, "▼ 0 B/s", sizeof(m->net_rx_text));
    g_strlcpy(m->net_tx_text, "▲ 0 B/s", sizeof(m->net_tx_text));
    load_config(m);
    sysmon_apply_css(m);

    m->event_box = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(m->event_box), FALSE);
    gtk_widget_set_has_tooltip(m->event_box, TRUE);
    m->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(m->box), "sysmon-box");
    gtk_container_add(GTK_CONTAINER(m->event_box), m->box);
    gtk_container_add(GTK_CONTAINER(plugin), m->event_box);
    xfce_panel_plugin_add_action_widget(plugin, m->event_box);

    xfce_panel_plugin_menu_show_configure(plugin);
    g_signal_connect(plugin, "configure-plugin", G_CALLBACK(prefs_open_dialog), m);
    g_signal_connect(m->event_box, "button-press-event", G_CALLBACK(on_button_press), m);
    g_signal_connect(m->event_box, "query-tooltip", G_CALLBACK(on_query_tooltip), m);
    g_signal_connect(plugin, "size-changed", G_CALLBACK(on_size_changed), m);
    g_signal_connect(plugin, "free-data", G_CALLBACK(on_free_data), m);

    sysmon_rebuild_ui(m);
    metrics_update(m);
    gtk_widget_show_all(m->event_box);
    sysmon_restart_timer(m);
}

XFCE_PANEL_PLUGIN_REGISTER(sysmon_construct)
