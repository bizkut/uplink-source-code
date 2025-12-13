---
description: How to build Uplink for Linux
---

## Build Uplink for Linux

// turbo-all

1. Build internal libraries:
```bash
cd /home/bizkut/Downloads/uplink-source-code/lib
make
```

2. Build tcp4u library:
```bash
cd /home/bizkut/Downloads/uplink-source-code/contrib/tcp4u.331/src
make -f ../Unix/Makefile
```

3. Build irclib library:
```bash
cd /home/bizkut/Downloads/uplink-source-code/contrib/irclib
make
```

4. Build unrar library:
```bash
cd /home/bizkut/Downloads/uplink-source-code/contrib/unrar
make
```

5. Build main game:
```bash
cd /home/bizkut/Downloads/uplink-source-code/uplink/src
make -f Makefile.linux
```

## Notes
- Uses `Makefile.linux` which has g++ instead of apg++
- FTGL linking currently blocked - needs bundled FTGL-2.1.2 built
