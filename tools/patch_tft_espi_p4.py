# Tab5 Monster C5 CSI Human Sensing -- a pre-build step for the tab5 env.
#
# TFT_eSPI 2.5.43 has no ESP32-P4 port. The app never drives a display bus
# through it (M5GFX owns the panel); it only needs TFT_eSPI's sprite and font
# code, so the P4 is routed to the library's Generic processor, which compiles.
Import("env")
import os

# ---------------------------------------------------------------------------
# TFT_eSPI, ESP32-P4. Only for the tab5 environments.
#
# The Tab5 never drives a bus through TFT_eSPI -- the library is kept as the
# TYPE the app's canvas needs (include/tab5_user_setup.h says why) -- so it
# does not need a P4 processor port at all, only something that COMPILES on
# one. Asked to build for a P4, 2.5.43 falls through to the plain-Xtensa
# processor and dies on its SPI registers, exactly as it does on the C5. The
# library's own Generic processor (plain Arduino SPI and digitalWrite) is the
# right answer here: two #elif lines route the P4 to it.
def patch_tft_espi_for_p4(env):
    lib = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env["PIOENV"], "TFT_eSPI")
    if not os.path.isdir(lib):
        raise Exception(
            "TFT_eSPI is not installed at %s yet, so the ESP32-P4 dispatch "
            "cannot be added. This script must run AFTER the library manager." % lib
        )
    edits = (
        ("TFT_eSPI.h",
         '#elif defined (ESP32)\n  #include "Processors/TFT_eSPI_ESP32.h"\n',
         '#elif defined(CONFIG_IDF_TARGET_ESP32P4)\n'
         '  #include "Processors/TFT_eSPI_Generic.h"\n'
         '  #define GENERIC_PROCESSOR\n'),
        ("TFT_eSPI.cpp",
         '#if defined (ESP32)\n  #if defined(CONFIG_IDF_TARGET_ESP32S3)\n',
         '#if defined(CONFIG_IDF_TARGET_ESP32P4)\n'
         '  #include "Processors/TFT_eSPI_Generic.c"\n'
         '#elif defined (ESP32)\n  #if defined(CONFIG_IDF_TARGET_ESP32S3)\n'),
    )
    # The Generic processor turns TFT_CS/TFT_DC into digitalWrite() calls
    # whenever the macro is DEFINED -- and the setup defines them as -1 (the
    # library needs TFT_CS defined for its own `if (TFT_CS >= 0)` tests).
    # digitalWrite(-1) is pin 255 on core 3, which logs an error line on every
    # startWrite()/endWrite() the app makes through its TFT_eSPI& references,
    # two a frame. A -1 pin is no pin: make the guards say so.
    gen = os.path.join(lib, "Processors", "TFT_eSPI_Generic.h")
    with open(gen, "r", encoding="utf-8", errors="surrogateescape") as f:
        text = f.read()
    changed = False
    for pin in ("TFT_DC", "TFT_CS"):
        old = "#ifndef %s\n" % pin
        new = "#if !defined(%s) || (%s < 0)\n" % (pin, pin)
        if text.count(old) == 1:
            text = text.replace(old, new)
            changed = True
        elif text.count(new) != 1:
            raise Exception("%s: expected one '%s' guard to relax, found %d" % (gen, pin, text.count(old)))
    if changed:
        with open(gen, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(text)
        print("TFT_eSPI: Generic processor treats TFT_CS/TFT_DC = -1 as no pin")
    # The .h gets the P4 branch added above its anchor; the .cpp has its
    # anchor REPLACED (its leading #if becomes the #elif after the new branch).
    for fname, anchor, addition in edits:
        path = os.path.join(lib, fname)
        with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
            text = f.read()
        if "CONFIG_IDF_TARGET_ESP32P4" in text:
            continue                      # already patched; runs are idempotent
        if text.count(anchor) != 1:
            raise Exception(
                "%s: expected exactly one anchor for the ESP32-P4 branch, found %d. "
                "TFT_eSPI has changed shape -- check whether it now supports the P4 on its own."
                % (fname, text.count(anchor))
            )
        text = text.replace(anchor, addition + anchor if fname.endswith(".h") else addition)
        with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(text)
        print("TFT_eSPI: added ESP32-P4 (Generic processor) branch to %s" % fname)


if env["PIOENV"] == "tab5":
    patch_tft_espi_for_p4(env)
