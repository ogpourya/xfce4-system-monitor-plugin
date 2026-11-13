# xfce4-system-monitor-plugin

Lightweight single-row XFCE 4 panel plugin showing **CPU**, **RAM**, **Network** (`▼` / `▲`), and **Disk** usage.

## Features

- **Click to launch task manager**: Left-click the widget to open `xfce4-taskmanager` (configurable in Properties).
- **Low-overhead polling**: Pre-opened `/proc` file descriptors with `pread()` and dirty-checked GTK label updates (`250 ms` default interval).
- **Theme-tinted symbolic icons**: Built-in crisp `16x16` symbolic SVGs automatically tinted to your panel foreground color, or any GTK icon name.
- **Live preferences**: Right-click → **Properties** to toggle metrics, pick network interface (`auto`, `all`, or specific NIC), set mount path, adjust update interval (`100–10000 ms`), font/icon sizes, and custom colors.

## Dependencies

- `gtk3` (`gtk+-3.0`)
- `xfce4-panel` (`libxfce4panel-2.0`)
- `libxfce4ui` (`libxfce4ui-2`)
- `libxfce4util` (`libxfce4util-1.0`)

## Build & Install

```bash
make
sudo make install
xfce4-panel -r
```

Then right-click your XFCE panel → **Panel** → **Add New Items...** → **System Monitor**.

## License

MIT
