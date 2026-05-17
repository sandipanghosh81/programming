# Qt 6 UI Setup in Docker

The Linux development container for this repository includes **Qt 6 development packages** so sub-projects can build UI layers using modern Qt.

## Installed package groups

The Docker image includes:

- `qt6-base-dev`
- `qt6-base-dev-tools`
- `qt6-declarative-dev`
- `qt6-declarative-dev-tools`
- `qt6-tools-dev`
- `qt6-tools-dev-tools`
- `qt6-svg-dev`
- `qt6-wayland-dev`

These provide support for common Qt Widgets and Qt Quick/QML development workflows.

## Verify inside the container

```bash
./linux-shell.sh
qmake6 --version
pkg-config --modversion Qt6Core
```

## Notes

- This gives you the **latest Qt 6 packages available from Ubuntu 24.04** in the container.
- Exact patch versions depend on the Ubuntu package repository snapshot used by the image.
- If future projects need more Qt modules, add them to `Dockerfile` and rebuild:

```bash
docker build -t cpp-linux-dev .
```
