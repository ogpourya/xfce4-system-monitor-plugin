CC      ?= gcc
CFLAGS  += -O3 -march=native -flto -Wall -Wextra -fPIC $(shell pkg-config --cflags gtk+-3.0 libxfce4panel-2.0 libxfce4util-1.0 libxfce4ui-2)
LDFLAGS += -shared -flto -Wl,-O1,--as-needed $(shell pkg-config --libs gtk+-3.0 libxfce4panel-2.0 libxfce4util-1.0 libxfce4ui-2)

SRCS        = system-monitor.c metrics.c prefs.c
HDRS        = sysmon.h
PLUGIN_DIR  = /usr/lib/xfce4/panel/plugins
DESKTOP_DIR = /usr/share/xfce4/panel/plugins

all: libsystem-monitor.so

libsystem-monitor.so: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

install: all
	install -Dm755 libsystem-monitor.so $(DESTDIR)$(PLUGIN_DIR)/libsystem-monitor.so
	install -Dm644 system-monitor.desktop $(DESTDIR)$(DESKTOP_DIR)/system-monitor.desktop

clean:
	rm -f libsystem-monitor.so
