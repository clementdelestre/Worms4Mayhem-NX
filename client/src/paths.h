#pragma once
// DATA_DIR holds the player's writable files; ASSETS_DIR the imported W4M data, on the SD card unless W4NX_BUNDLED_ASSETS packs it in romfs
#ifdef __SWITCH__
#define DATA_DIR "sdmc:/switch/worms4nx/"
#define ROMFS_DIR "romfs:/"
#else
#define DATA_DIR "./"
#define ROMFS_DIR "./romfs/"
#endif
#ifdef W4NX_BUNDLED_ASSETS
#define ASSETS_BASE ROMFS_DIR
#else
#define ASSETS_BASE DATA_DIR
#endif
#define ASSETS_DIR ASSETS_BASE "assets/"
