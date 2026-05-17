# LEMON Graph Library Setup

This repository is configured to use the **LEMON** graph library in both the Linux Docker environment and on macOS.

## Linux container

The Docker image installs:

```bash
liblemon-dev
```

Verify inside the container:

```bash
./linux-shell.sh
pkg-config --modversion lemon
ls /usr/include/lemon
```

## macOS

On macOS, LEMON is installed via the maintained Homebrew tap:

```bash
brew tap The-OpenROAD-Project/homebrew-lemon-graph
brew install The-OpenROAD-Project/homebrew-lemon-graph/lemon-graph
```

Typical verification commands:

```bash
export PKG_CONFIG_PATH="/opt/homebrew/opt/lemon-graph/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
pkg-config --modversion lemon
ls /opt/homebrew/opt/lemon-graph/include/lemon
```

## Notes

- LEMON is a portable C++ graph and optimization library.
- It is a good fit for graph algorithms, flows, matchings, and network optimization problems.
- If future projects depend on it, prefer linking through `pkg-config` where possible.
