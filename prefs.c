// SPDX-License-Identifier: MIT
#include "sysmon.h"
#include <libxfce4ui/libxfce4ui.h>
#include <string.h>

typedef struct {
    SysMon *m;
    GtkWidget *cb_metric[METRIC_COUNT];
    GtkWidget *cb_icons;
    GtkWidget *entry_taskmgr;
    GtkWidget *combo_iface, *entry_disk;
    GtkWidget *spin_interval, *spin_icon, *spin_font;
    GtkWidget *entry_icon[METRIC_COUNT];
    GtkWidget *col_metric[METRIC_COUNT];
    GtkWidget *col_rx, *col_tx, *col_dot;
} PrefsUI;

static void set_color_btn(GtkColorButton *btn, const char *hex) {
    GdkRGBA rgba;
    if (hex && *hex && gdk_rgba_parse(&rgba, hex)) {
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(btn), &rgba);
    } else {
        gdk_rgba_parse(&rgba, "rgba(0,0,0,0)");
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(btn), &rgba);
    }
}

static gchar *get_color_btn_hex(GtkColorButton *btn) {
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &rgba);
    if (rgba.alpha < 0.05) return g_strdup("");
    return g_strdup_printf("#%02x%02x%02x",
        (int)(rgba.red * 255.0 + 0.5),
        (int)(rgba.green * 255.0 + 0.5),
        (int)(rgba.blue * 255.0 + 0.5));
}

static void on_prefs_changed(GtkWidget *w, gpointer user_data) {
    (void)w;
    PrefsUI *ui = user_data;
    SysMon *m = ui->m;

    for (int i = 0; i < METRIC_COUNT; i++) {
        m->metrics[i].enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->cb_metric[i]));
        if (ui->entry_icon[i]) {
            const char *txt = gtk_entry_get_text(GTK_ENTRY(ui->entry_icon[i]));
            sysmon_parse_icon_spec(&m->metrics[i].icon, txt, (BuiltinIcon)i);
        }
        if (ui->col_metric[i]) {
            g_free(m->metrics[i].color);
            m->metrics[i].color = get_color_btn_hex(GTK_COLOR_BUTTON(ui->col_metric[i]));
        }
    }

    m->show_icons = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->cb_icons));

    g_free(m->task_manager_cmd);
    m->task_manager_cmd = g_strdup(gtk_entry_get_text(GTK_ENTRY(ui->entry_taskmgr)));

    g_autofree gchar *iface = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(ui->combo_iface));
    if (iface) {
        g_autofree gchar *new_if = NULL;
        if (g_str_has_prefix(iface, "auto")) new_if = g_strdup("auto");
        else if (g_str_has_prefix(iface, "all")) new_if = g_strdup("all");
        else new_if = g_strdup(iface);
        if (g_strcmp0(m->net_iface, new_if) != 0) {
            g_free(m->net_iface);
            m->net_iface = g_steal_pointer(&new_if);
            m->cached_auto_iface[0] = '\0';
            m->net_hist_len = 0;
            m->net_hist_idx = 0;
        }
    }

    g_free(m->disk_path);
    m->disk_path = g_strdup(gtk_entry_get_text(GTK_ENTRY(ui->entry_disk)));

    int new_int = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui->spin_interval));
    if (new_int != m->interval_ms) {
        m->interval_ms = new_int;
        sysmon_restart_timer(m);
    }
    m->icon_size = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui->spin_icon));
    m->font_size = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui->spin_font));

    g_free(m->net_rx_color); m->net_rx_color = get_color_btn_hex(GTK_COLOR_BUTTON(ui->col_rx));
    g_free(m->net_tx_color); m->net_tx_color = get_color_btn_hex(GTK_COLOR_BUTTON(ui->col_tx));
    g_free(m->dot_color);    m->dot_color    = get_color_btn_hex(GTK_COLOR_BUTTON(ui->col_dot));

    m->tick = 0;
    sysmon_apply_css(m);
    sysmon_rebuild_ui(m);
    metrics_update(m);
    sysmon_schedule_save(m);
}

static void populate_ifaces(GtkComboBoxText *combo, const char *current) {
    gtk_combo_box_text_append_text(combo, "auto (physical default route)");
    gtk_combo_box_text_append_text(combo, "all (sum non-loopback)");

    int active_idx = (g_strcmp0(current, "all") == 0) ? 1 : 0;
    GDir *dir = g_dir_open("/sys/class/net", 0, NULL);
    int idx = 2;
    gboolean found_custom = FALSE;
    if (dir) {
        const char *name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            gtk_combo_box_text_append_text(combo, name);
            if (g_strcmp0(current, name) == 0) {
                active_idx = idx;
                found_custom = TRUE;
            }
            idx++;
        }
        g_dir_close(dir);
    }
    if (current && *current && strcmp(current, "auto") != 0 && strcmp(current, "all") != 0 && !found_custom) {
        gtk_combo_box_text_append_text(combo, current);
        active_idx = idx;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active_idx);
}

static void on_reset_color_clicked(GtkButton *b, gpointer user_data) {
    (void)user_data;
    GtkColorButton *btn = g_object_get_data(G_OBJECT(b), "target-btn");
    PrefsUI *ui = g_object_get_data(G_OBJECT(b), "prefs-ui");
    set_color_btn(btn, "");
    on_prefs_changed(GTK_WIDGET(btn), ui);
}

static GtkWidget *make_color_row(PrefsUI *ui, GtkGrid *grid, int row, const char *label, const char *hex) {
    GtkWidget *lbl = gtk_label_new(label);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    GtkWidget *btn = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(btn), TRUE);
    set_color_btn(GTK_COLOR_BUTTON(btn), hex);
    g_signal_connect(btn, "color-set", G_CALLBACK(on_prefs_changed), ui);

    GtkWidget *rst = gtk_button_new_with_label("Theme");
    gtk_widget_set_tooltip_text(rst, "Use panel theme default color");
    g_object_set_data(G_OBJECT(rst), "target-btn", btn);
    g_object_set_data(G_OBJECT(rst), "prefs-ui", ui);
    g_signal_connect(rst, "clicked", G_CALLBACK(on_reset_color_clicked), NULL);

    gtk_grid_attach(grid, lbl, 0, row, 1, 1);
    gtk_grid_attach(grid, btn, 1, row, 1, 1);
    gtk_grid_attach(grid, rst, 2, row, 1, 1);
    return btn;
}

static void on_reset_icons_clicked(GtkButton *b, gpointer user_data) {
    (void)b;
    PrefsUI *ui = user_data;
    for (int i = 0; i < METRIC_COUNT; i++) {
        if (ui->entry_icon[i] && METRIC_META[i].default_icon)
            gtk_entry_set_text(GTK_ENTRY(ui->entry_icon[i]), METRIC_META[i].default_icon);
    }
}

static void on_prefs_response(GtkDialog *dlg, gint resp, gpointer user_data) {
    (void)resp;
    PrefsUI *ui = user_data;
    sysmon_flush_save(ui->m);
    xfce_panel_plugin_unblock_menu(ui->m->plugin);
    gtk_widget_destroy(GTK_WIDGET(dlg));
    g_free(ui);
}

static GtkWidget *new_grid(void) {
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 8);
    gtk_grid_set_column_spacing(GTK_GRID(g), 12);
    gtk_container_set_border_width(GTK_CONTAINER(g), 12);
    return g;
}

void prefs_open_dialog(XfcePanelPlugin *plugin, gpointer user_data) {
    SysMon *m = user_data;
    xfce_panel_plugin_block_menu(plugin);

    GtkWidget *dlg = xfce_titled_dialog_new_with_mixed_buttons(
        "System Monitor Settings",
        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(plugin))),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "window-close-symbolic", "_Close", GTK_RESPONSE_OK,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 420, -1);

    PrefsUI *ui = g_new0(PrefsUI, 1);
    ui->m = m;

    GtkWidget *nb = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(nb), 8);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), nb, TRUE, TRUE, 0);

    /* Tab 1: General */
    GtkWidget *g1 = new_grid();
    for (int i = 0; i < METRIC_COUNT; i++) {
        ui->cb_metric[i] = gtk_check_button_new_with_label(METRIC_META[i].toggle_label);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->cb_metric[i]), m->metrics[i].enabled);
        gtk_grid_attach(GTK_GRID(g1), ui->cb_metric[i], i % 2, i / 2, 1, 1);
        g_signal_connect(ui->cb_metric[i], "toggled", G_CALLBACK(on_prefs_changed), ui);
    }

    ui->cb_icons = gtk_check_button_new_with_label("Show Icons");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->cb_icons), m->show_icons);
    gtk_grid_attach(GTK_GRID(g1), ui->cb_icons, 0, 2, 1, 1);
    g_signal_connect(ui->cb_icons, "toggled", G_CALLBACK(on_prefs_changed), ui);

    GtkWidget *lbl_if = gtk_label_new("Network interface:");
    gtk_widget_set_halign(lbl_if, GTK_ALIGN_START);
    ui->combo_iface = gtk_combo_box_text_new();
    populate_ifaces(GTK_COMBO_BOX_TEXT(ui->combo_iface), m->net_iface);
    gtk_grid_attach(GTK_GRID(g1), lbl_if, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(g1), ui->combo_iface, 1, 3, 1, 1);
    g_signal_connect(ui->combo_iface, "changed", G_CALLBACK(on_prefs_changed), ui);

    GtkWidget *lbl_dk = gtk_label_new("Disk mount path:");
    gtk_widget_set_halign(lbl_dk, GTK_ALIGN_START);
    ui->entry_disk = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ui->entry_disk), m->disk_path);
    gtk_grid_attach(GTK_GRID(g1), lbl_dk, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(g1), ui->entry_disk, 1, 4, 1, 1);
    g_signal_connect(ui->entry_disk, "changed", G_CALLBACK(on_prefs_changed), ui);

    GtkWidget *lbl_iv = gtk_label_new("Update interval (ms):");
    gtk_widget_set_halign(lbl_iv, GTK_ALIGN_START);
    ui->spin_interval = gtk_spin_button_new_with_range(100, 10000, 50);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_interval), m->interval_ms);
    gtk_grid_attach(GTK_GRID(g1), lbl_iv, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(g1), ui->spin_interval, 1, 5, 1, 1);
    g_signal_connect(ui->spin_interval, "value-changed", G_CALLBACK(on_prefs_changed), ui);

    GtkWidget *lbl_tm = gtk_label_new("Task manager command:");
    gtk_widget_set_halign(lbl_tm, GTK_ALIGN_START);
    ui->entry_taskmgr = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ui->entry_taskmgr), m->task_manager_cmd);
    gtk_grid_attach(GTK_GRID(g1), lbl_tm, 0, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(g1), ui->entry_taskmgr, 1, 6, 1, 1);
    g_signal_connect(ui->entry_taskmgr, "changed", G_CALLBACK(on_prefs_changed), ui);

    gtk_notebook_append_page(GTK_NOTEBOOK(nb), g1, gtk_label_new("General"));

    /* Tab 2: Icons & Size */
    GtkWidget *g2 = new_grid();
    GtkWidget *lbl_fs = gtk_label_new("Font size (px):");
    gtk_widget_set_halign(lbl_fs, GTK_ALIGN_START);
    ui->spin_font = gtk_spin_button_new_with_range(7, 24, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_font), m->font_size);
    gtk_grid_attach(GTK_GRID(g2), lbl_fs, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g2), ui->spin_font, 1, 0, 1, 1);
    g_signal_connect(ui->spin_font, "value-changed", G_CALLBACK(on_prefs_changed), ui);

    GtkWidget *lbl_is = gtk_label_new("Icon size (px):");
    gtk_widget_set_halign(lbl_is, GTK_ALIGN_START);
    ui->spin_icon = gtk_spin_button_new_with_range(8, 32, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_icon), m->icon_size);
    gtk_grid_attach(GTK_GRID(g2), lbl_is, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g2), ui->spin_icon, 1, 1, 1, 1);
    g_signal_connect(ui->spin_icon, "value-changed", G_CALLBACK(on_prefs_changed), ui);

    const char *icon_labels[METRIC_COUNT] = {"CPU icon:", "RAM icon:", NULL, "Disk icon:"};
    int row = 2;
    for (int i = 0; i < METRIC_COUNT; i++) {
        if (!icon_labels[i]) continue;
        GtkWidget *lbl = gtk_label_new(icon_labels[i]);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        ui->entry_icon[i] = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(ui->entry_icon[i]), m->metrics[i].icon.name ? m->metrics[i].icon.name : "");
        gtk_grid_attach(GTK_GRID(g2), lbl, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(g2), ui->entry_icon[i], 1, row, 1, 1);
        g_signal_connect(ui->entry_icon[i], "changed", G_CALLBACK(on_prefs_changed), ui);
        row++;
    }

    GtkWidget *btn_rst_icons = gtk_button_new_with_label("Reset Built-in Icons");
    g_signal_connect(btn_rst_icons, "clicked", G_CALLBACK(on_reset_icons_clicked), ui);
    gtk_grid_attach(GTK_GRID(g2), btn_rst_icons, 1, row, 1, 1);

    gtk_notebook_append_page(GTK_NOTEBOOK(nb), g2, gtk_label_new("Icons & Size"));

    /* Tab 3: Colors */
    GtkWidget *g3 = new_grid();
    ui->col_metric[METRIC_CPU]  = make_color_row(ui, GTK_GRID(g3), 0, "CPU color:", m->metrics[METRIC_CPU].color);
    ui->col_metric[METRIC_RAM]  = make_color_row(ui, GTK_GRID(g3), 1, "RAM color:", m->metrics[METRIC_RAM].color);
    ui->col_rx                    = make_color_row(ui, GTK_GRID(g3), 2, "Download (▼) color:", m->net_rx_color);
    ui->col_tx                    = make_color_row(ui, GTK_GRID(g3), 3, "Upload (▲) color:", m->net_tx_color);
    ui->col_metric[METRIC_DISK] = make_color_row(ui, GTK_GRID(g3), 4, "Disk color:", m->metrics[METRIC_DISK].color);
    ui->col_dot                   = make_color_row(ui, GTK_GRID(g3), 5, "Separator color:", m->dot_color);

    gtk_notebook_append_page(GTK_NOTEBOOK(nb), g3, gtk_label_new("Colors"));

    g_signal_connect(dlg, "response", G_CALLBACK(on_prefs_response), ui);
    gtk_widget_show_all(dlg);
}
