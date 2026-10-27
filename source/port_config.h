/* port_config.h -- Modern Combat 4: Zero Hour for Switch (android32).
 *
 * The Android release 1.2.3e (com.gameloft.android.ANMP.GloftM4HM),
 * lib/armeabi-v7a/libModernCombat4.so: Gameloft's GLF engine (glitch,
 * gameswf, Havok, vox), GLES 2, softfp. One native library (libgenerator.so
 * ships beside it, unused); the Java side is answered by mc4_jni.c.
 * MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#define PORT_TITLE   "Modern Combat 4"
#define PORT_NAME    "mc4_nx"
#define PORT_PACKAGE "com.gameloft.android.ANMP.GloftM4HM"

/* The one library, and the build it must be: the addresses in mc4_main.c
 * (menu manager) belong to this exact file. Its CRC-32 is the APK entry's,
 * which the setup stamps (no extra read at boot). */
#define MC4_LIB          "libModernCombat4.so"
#define MC4_LIB_CRC32    0xf9bc349fUL
#define MC4_LIB_SIZE     24855648UL
#define MC4_VERSION      "1.2.3e"

/* The expansion files, as the game's own installer names them. They stay
 * zips: the engine reads them itself (getMainFileLocation /
 * getPatchFileLocation). */
#define MC4_OBB_MAIN     "main.1100." PORT_PACKAGE ".obb"
#define MC4_OBB_PATCH    "patch.12222." PORT_PACKAGE ".obb"

/* The game's SD folder (MC4.getSDFolder): its saves, settings and, made by
 * the setup here, videos/ (the logo and briefings out of the main OBB). */
#define MC4_SD_FOLDER    "/sdcard/gameloft/games/GloftM4HM"

/* vox: AudioTrack through JNI (mc4_audio.c); OpenSL ES refused. */
#define RT_OPENSLES 0

#endif /* PORT_CONFIG_H */
