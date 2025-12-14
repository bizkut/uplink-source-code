---
description: How to build Uplink for Linux
---

# Build Uplink

// turbo-all

```bash
# 1. Build libraries
cd /home/bizkut/Downloads/uplink-source-code/lib && make

# 2. Build contrib
cd /home/bizkut/Downloads/uplink-source-code/contrib/tcp4u.331/src && make -f ../Unix/Makefile
cd /home/bizkut/Downloads/uplink-source-code/contrib/irclib && make
cd /home/bizkut/Downloads/uplink-source-code/contrib/unrar && make

# 3. Build game
cd /home/bizkut/Downloads/uplink-source-code/uplink/src && make -f Makefile.linux
```

## Output
- `uplink/src/uplink.full` - Main executable
