# Area Play — Vita Edition: building from source

You need [VitaSDK](https://vitasdk.org) (with the `VITASDK` environment variable set), CMake and Ninja.

```
build.cmd          (Windows)
./build.sh         (Linux / macOS / WSL)
```

The VPK is written to `build/AreaPlay.vpk`.

`src/main.c` includes the files of `src/parties/` (one build unit): do not add them to CMake.
Files in `assets/` are packed into the VPK automatically.
