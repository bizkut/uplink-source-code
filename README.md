# Uplink Source Code

Linux port of Uplink with UplinkOS mod HD UI integration.

## Quick Start
```bash
# Build
cd uplink/src && make -f Makefile.linux

# Run
cd ../bin && ./uplink.full
```

## Documentation

| Doc | Purpose |
|-----|---------|
| `docs/PROJECT_CONTEXT.md` | **Start here** - Project overview, directory structure |
| `docs/MOD_FIXES.md` | Bug fix tracking and HD UI progress |
| `docs/ARCHITECTURE.md` | Source code navigation |
| `.agent/workflows/` | Agent task workflows |

## Agent Workflows

| Workflow | Command |
|----------|---------|
| `/build` | Build the project |
| `/integrate-mod-fix` | Port a fix from decompiled mod |
| `/hd-ui` | HD UI development |

## Key Directories

```
uplink/src/          # Game source
uplink/src/hd_ui/    # HD UI framework (Allegro5)
uplink/mod/          # Reference: decompiled mod
uplink/bin/uplinkHD/ # HD UI assets (layouts, fonts)
```

## Current Focus: HD UI Port
See `.agent/workflows/hd-ui.md` for HD UI development tasks.
