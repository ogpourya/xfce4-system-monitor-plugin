// SPDX-License-Identifier: MIT
#include "sysmon.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

static void close_fd(int *fd) {
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static ssize_t pread_all(int fd, char *buf, size_t cap) {
    if (fd < 0 || cap == 0) return -1;
    ssize_t n = pread(fd, buf, cap - 1, 0);
    if (n > 0) buf[n] = '\0';
    return n;
}

void metrics_init_fds(SysMon *m) {
    m->fd_stat    = open("/proc/stat", O_RDONLY | O_CLOEXEC);
    m->fd_meminfo = open("/proc/meminfo", O_RDONLY | O_CLOEXEC);
    m->fd_route   = open("/proc/net/route", O_RDONLY | O_CLOEXEC);
    m->fd_net_dev = open("/proc/net/dev", O_RDONLY | O_CLOEXEC);
}

void metrics_close_fds(SysMon *m) {
    close_fd(&m->fd_stat);
    close_fd(&m->fd_meminfo);
    close_fd(&m->fd_route);
    close_fd(&m->fd_net_dev);
}

static inline void format_speed(double bps, char *out, size_t len) {
    if (bps < 1000.0) {
        g_snprintf(out, len, "%d B/s", (int)bps);
        return;
    }
    static const char *const units[] = {"KB/s", "MB/s", "GB/s", "TB/s"};
    double v = bps;
    int i = -1;
    while (v >= 1000.0 && i < 3) { v *= 0.001; i++; }
    if (v >= 999.95 && i < 3) { v *= 0.001; i++; }
    if (i <= 0) g_snprintf(out, len, "%.0f %s", v, units[i]);
    else g_snprintf(out, len, "%.1f %s", v, units[i]);
}

static int window_samples(int interval_ms) {
    int win = 1000 / MAX(1, interval_ms);
    return CLAMP(win, 1, HIST_LEN - 1);
}

static void read_cpu(SysMon *m) {
    char buf[256];
    if (pread_all(m->fd_stat, buf, sizeof(buf)) <= 0) return;

    unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iow = 0, irq = 0, sirq = 0, steal = 0;
    if (sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &sys, &idle, &iow, &irq, &sirq, &steal) < 7)
        return;

    unsigned long long total = user + nice + sys + idle + iow + irq + sirq + steal;
    unsigned long long idle_total = idle + iow;

    m->cpu_hist[m->cpu_hist_idx] = (CpuSample){ .idle = idle_total, .total = total };
    if (m->cpu_hist_len < HIST_LEN) m->cpu_hist_len++;

    int back = MIN(window_samples(m->interval_ms), m->cpu_hist_len - 1);
    if (back >= 1) {
        int old_idx = (m->cpu_hist_idx - back + HIST_LEN) % HIST_LEN;
        CpuSample old = m->cpu_hist[old_idx];
        unsigned long long dt = total >= old.total ? total - old.total : 0;
        unsigned long long di = idle_total >= old.idle ? idle_total - old.idle : 0;
        if (dt > 0 && dt >= di) {
            unsigned long long busy = dt - di;
            m->metrics[METRIC_CPU].pct = (int)((busy * 100ULL + dt / 2ULL) / dt);
        } else {
            m->metrics[METRIC_CPU].pct = 0;
        }
    }
    m->cpu_hist_idx = (m->cpu_hist_idx + 1) % HIST_LEN;
}

static void read_ram(SysMon *m) {
    char buf[512];
    if (pread_all(m->fd_meminfo, buf, sizeof(buf)) <= 0) return;

    unsigned long long total = 0, avail = 0;
    char *p = buf;
    while (*p && (!total || !avail)) {
        if (strncmp(p, "MemTotal:", 9) == 0)
            total = strtoull(p + 9, NULL, 10);
        else if (strncmp(p, "MemAvailable:", 13) == 0)
            avail = strtoull(p + 13, NULL, 10);
        char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    if (total > 0 && avail <= total) {
        unsigned long long used = total - avail;
        m->metrics[METRIC_RAM].pct = (int)((used * 100ULL + total / 2ULL) / total);
    }
}

static gboolean is_virtual_iface(const char *name) {
    if (strcmp(name, "lo") == 0) return TRUE;
    static const char *const prefixes[] = {
        "tun", "tap", "wg", "tailscale", "zt", "virbr", "docker", "br-", "veth", "waydroid", "happ-", NULL
    };
    for (int i = 0; prefixes[i]; i++)
        if (g_str_has_prefix(name, prefixes[i])) return TRUE;
    char dev_path[128];
    g_snprintf(dev_path, sizeof(dev_path), "/sys/class/net/%s/device", name);
    return access(dev_path, F_OK) != 0;
}

gboolean metrics_active_iface(SysMon *m, char *out, size_t len) {
    char buf[2048];
    if (pread_all(m->fd_route, buf, sizeof(buf)) <= 0) return FALSE;

    int metric = 0;
    int best_phys_metric = 1 << 30, best_any_metric = 1 << 30;
    char best_phys[64] = {0}, best_any[64] = {0};

    char *p = strchr(buf, '\n');
    while (p && *++p) {
        char iface[64], dest[32];
        unsigned int flags = 0;
        if (sscanf(p, "%63s %31s %*31s %x %*u %*u %d", iface, dest, &flags, &metric) == 4) {
            if (strcmp(dest, "00000000") == 0 && (flags & 0x1)) {
                if (!is_virtual_iface(iface) && metric < best_phys_metric) {
                    best_phys_metric = metric;
                    g_strlcpy(best_phys, iface, sizeof(best_phys));
                }
                if (metric < best_any_metric) {
                    best_any_metric = metric;
                    g_strlcpy(best_any, iface, sizeof(best_any));
                }
            }
        }
        p = strchr(p, '\n');
    }
    if (best_phys[0]) { g_strlcpy(out, best_phys, len); return TRUE; }
    if (best_any[0])  { g_strlcpy(out, best_any, len);  return TRUE; }
    return FALSE;
}

static void read_net(SysMon *m, gboolean refresh_route) {
    char want[64] = {0};
    gboolean filter_one = FALSE;

    if (!m->net_iface || strcmp(m->net_iface, "auto") == 0) {
        if (refresh_route || m->cached_auto_iface[0] == '\0') {
            if (!metrics_active_iface(m, m->cached_auto_iface, sizeof(m->cached_auto_iface)))
                m->cached_auto_iface[0] = '\0';
        }
        if (m->cached_auto_iface[0] != '\0') {
            g_strlcpy(want, m->cached_auto_iface, sizeof(want));
            filter_one = TRUE;
        }
    } else if (strcmp(m->net_iface, "all") != 0) {
        g_strlcpy(want, m->net_iface, sizeof(want));
        filter_one = TRUE;
    }

    char buf[4096];
    if (pread_all(m->fd_net_dev, buf, sizeof(buf)) <= 0) return;

    unsigned long long rx = 0, tx = 0;
    char *p = strchr(buf, '\n');
    if (p) p = strchr(p + 1, '\n');
    while (p && *++p) {
        char *eol = strchr(p, '\n');
        if (eol) *eol = '\0';
        char *colon = strchr(p, ':');
        if (colon) {
            *colon = '\0';
            char *name = g_strstrip(p);
            gboolean match = filter_one ? (strcmp(name, want) == 0) : (strcmp(name, "lo") != 0);
            if (match) {
                unsigned long long r_bytes = 0, t_bytes = 0;
                if (sscanf(colon + 1, "%llu %*u %*u %*u %*u %*u %*u %*u %llu",
                           &r_bytes, &t_bytes) == 2) {
                    rx += r_bytes;
                    tx += t_bytes;
                }
                if (filter_one) break;
            }
        }
        p = eol;
    }

    const char *key = filter_one ? want : "__ALL__";
    gint64 now = g_get_monotonic_time();
    if (strcmp(m->prev_iface, key) != 0) {
        g_strlcpy(m->prev_iface, key, sizeof(m->prev_iface));
        m->net_hist_len = 0;
        m->net_hist_idx = 0;
    }

    m->net_hist[m->net_hist_idx] = (NetSample){ .t_us = now, .rx = rx, .tx = tx };
    if (m->net_hist_len < HIST_LEN) m->net_hist_len++;

    int back = MIN(window_samples(m->interval_ms), m->net_hist_len - 1);
    if (back < 1) {
        m->net_hist_idx = (m->net_hist_idx + 1) % HIST_LEN;
        g_strlcpy(m->net_rx_text, "▼ 0 B/s", sizeof(m->net_rx_text));
        g_strlcpy(m->net_tx_text, "▲ 0 B/s", sizeof(m->net_tx_text));
        return;
    }

    int old_idx = (m->net_hist_idx - back + HIST_LEN) % HIST_LEN;
    NetSample old = m->net_hist[old_idx];
    m->net_hist_idx = (m->net_hist_idx + 1) % HIST_LEN;

    double dt = (double)(now - old.t_us) * 1e-6;
    if (dt < 0.05) return;
    double rx_spd = rx >= old.rx ? (double)(rx - old.rx) / dt : 0.0;
    double tx_spd = tx >= old.tx ? (double)(tx - old.tx) / dt : 0.0;

    char rx_s[16], tx_s[16];
    format_speed(rx_spd, rx_s, sizeof(rx_s));
    format_speed(tx_spd, tx_s, sizeof(tx_s));
    g_snprintf(m->net_rx_text, sizeof(m->net_rx_text), "▼ %s", rx_s);
    g_snprintf(m->net_tx_text, sizeof(m->net_tx_text), "▲ %s", tx_s);
}

static void read_disk(SysMon *m) {
    const char *path = (m->disk_path && *m->disk_path) ? m->disk_path : "/";
    struct statvfs st;
    if (statvfs(path, &st) == 0 && st.f_blocks > 0) {
        unsigned long long used = st.f_blocks - st.f_bfree;
        m->metrics[METRIC_DISK].pct = (int)((used * 100ULL + st.f_blocks / 2ULL) / st.f_blocks);
    }
}

gboolean metrics_update(gpointer user_data) {
    SysMon *m = user_data;
    guint slow_div = (guint)MAX(1, 2000 / m->interval_ms);
    gboolean slow_tick = (m->tick % slow_div) == 0;
    m->tick++;

    if (m->metrics[METRIC_CPU].enabled)  read_cpu(m);
    if (m->metrics[METRIC_RAM].enabled)  read_ram(m);
    if (m->metrics[METRIC_NET].enabled)  read_net(m, slow_tick);
    if (m->metrics[METRIC_DISK].enabled && slow_tick) read_disk(m);

    char buf[16];
    for (int i = 0; i < METRIC_COUNT; i++) {
        MetricSlot *s = &m->metrics[i];
        if (s->label && s->pct != s->disp_pct) {
            s->disp_pct = s->pct;
            g_snprintf(buf, sizeof(buf), "%d%%", s->pct);
            gtk_label_set_text(GTK_LABEL(s->label), buf);
        }
    }
    if (m->net_rx_label && strcmp(m->net_rx_text, m->disp_net_rx) != 0) {
        g_strlcpy(m->disp_net_rx, m->net_rx_text, sizeof(m->disp_net_rx));
        gtk_label_set_text(GTK_LABEL(m->net_rx_label), m->net_rx_text);
    }
    if (m->net_tx_label && strcmp(m->net_tx_text, m->disp_net_tx) != 0) {
        g_strlcpy(m->disp_net_tx, m->net_tx_text, sizeof(m->disp_net_tx));
        gtk_label_set_text(GTK_LABEL(m->net_tx_label), m->net_tx_text);
    }
    return G_SOURCE_CONTINUE;
}
