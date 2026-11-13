// SPDX-License-Identifier: MIT
#ifndef SYSMON_H
#define SYSMON_H

#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>

#define HIST_LEN 8

typedef enum {
    METRIC_CPU = 0,
    METRIC_RAM,
    METRIC_NET,
    METRIC_DISK,
    METRIC_COUNT
} MetricId;

typedef enum {
    ICON_NONE = 0,
    ICON_BUILTIN,
    ICON_THEME
} IconKind;

typedef enum {
    BUILTIN_CPU = 0,
    BUILTIN_RAM,
    BUILTIN_DISK
} BuiltinIcon;

typedef struct {
    IconKind kind;
    BuiltinIcon builtin;
    gchar *name;
} IconSpec;

typedef struct {
    const char *show_key;
    const char *icon_key;
    const char *default_icon;
    const char *color_key;
    const char *css_class;
    const char *toggle_label;
} MetricMeta;

typedef struct {
    gboolean enabled;
    IconSpec icon;
    gchar *color;

    int pct;
    int disp_pct;
    GtkWidget *label;
} MetricSlot;

typedef struct {
    unsigned long long idle;
    unsigned long long total;
} CpuSample;

typedef struct {
    gint64 t_us;
    unsigned long long rx;
    unsigned long long tx;
} NetSample;

typedef struct {
    XfcePanelPlugin *plugin;
    GtkCssProvider *css;

    /* Configuration */
    MetricSlot metrics[METRIC_COUNT];
    gboolean show_icons;
    gboolean click_to_cycle;
    int cycle_state;

    gchar *net_iface;       /* "auto", "all", or interface name */
    gchar *disk_path;       /* default "/" */
    gint interval_ms;       /* 100..10000, default 250 */
    gint icon_size;         /* 8..32, default 13 */
    gint font_size;         /* 7..24, default 10 */

    gchar *net_rx_color;
    gchar *net_tx_color;
    gchar *dot_color;

    /* Widgets */
    GtkWidget *event_box;
    GtkWidget *box;
    GtkWidget *net_rx_label;
    GtkWidget *net_tx_label;

    /* Timers & pre-opened /proc FDs */
    guint timeout_id;
    guint save_timeout_id;
    guint tick;

    int fd_stat;
    int fd_meminfo;
    int fd_route;
    int fd_net_dev;

    /* Sliding-window sampling state */
    CpuSample cpu_hist[HIST_LEN];
    int cpu_hist_len;
    int cpu_hist_idx;

    NetSample net_hist[HIST_LEN];
    int net_hist_len;
    int net_hist_idx;
    char cached_auto_iface[64];
    char prev_iface[64];

    char net_rx_text[24];
    char net_tx_text[24];
    char disp_net_rx[24];
    char disp_net_tx[24];
} SysMon;

extern const MetricMeta METRIC_META[METRIC_COUNT];

/* Core lifecycle & UI (system-monitor.c) */
void sysmon_parse_icon_spec(IconSpec *spec, const char *raw, BuiltinIcon fallback);
void sysmon_apply_css(SysMon *m);
void sysmon_rebuild_ui(SysMon *m);
void sysmon_restart_timer(SysMon *m);
void sysmon_schedule_save(SysMon *m);
void sysmon_flush_save(SysMon *m);
int  sysmon_enabled_count(const SysMon *m);

/* Metrics polling (metrics.c) */
void     metrics_init_fds(SysMon *m);
void     metrics_close_fds(SysMon *m);
gboolean metrics_active_iface(SysMon *m, char *out, size_t len);
gboolean metrics_update(gpointer user_data);

/* Preferences dialog (prefs.c) */
void prefs_open_dialog(XfcePanelPlugin *plugin, gpointer user_data);

#endif
