#!/usr/bin/env python3
"""Exercise the production FMV detector with video and XA music signals.

This test compiles a small native fixture; run it locally, not in a no-build
agent session. Device queries are stubbed; the detector body is production C.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from test_netplay_load_apply_flow import function_body


def fixture_source():
    source = (Path(__file__).resolve().parents[1] / "src/psx_netplay_rb.c").read_text(
        encoding="utf-8")
    detector = function_body(source, "static int rb_fmv_media_active(void)\n{")
    hysteresis = re.search(r"^#define RB_FMV_MDEC_HYSTERESIS\s+(\d+u?)", source, re.M)
    if not hysteresis:
        raise AssertionError("production MDEC hysteresis definition is missing")
    return "#define RB_FMV_MDEC_HYSTERESIS " + hysteresis.group(1) + "\n" + r'''
#include <assert.h>
#include <stdio.h>
static int depth24, recent_mdec, xa_audio;
static unsigned xa_queries;
static int gpu_display_is_depth24(void) { return depth24; }
static int mdec_recently_active(unsigned window) {
    assert(window == RB_FMV_MDEC_HYSTERESIS);
    return recent_mdec;
}
int cdrom_xa_stream_active(void) { ++xa_queries; return xa_audio; }
static int rb_fmv_media_active(void) {
''' + detector + r'''
}
int main(void) {
    /* Every video/audio combination, including silent MDEC and 24-bit movies. */
    for (depth24 = 0; depth24 <= 1; ++depth24)
        for (recent_mdec = 0; recent_mdec <= 1; ++recent_mdec)
            for (xa_audio = 0; xa_audio <= 1; ++xa_audio)
                assert(rb_fmv_media_active() == (depth24 || recent_mdec));

    /* Movie -> fight: music continues after visual evidence expires. */
    xa_audio = 1;
    depth24 = 1; recent_mdec = 1;
    assert(rb_fmv_media_active());
    depth24 = 0;
    assert(rb_fmv_media_active()); /* recent MDEC still protects the movie tail */
    recent_mdec = 0;
    for (int frame = 0; frame < 600; ++frame)
        assert(!rb_fmv_media_active()); /* music must not hold a whole match */

    /* A later silent movie must re-enter protection normally. */
    xa_audio = 0; recent_mdec = 1;
    assert(rb_fmv_media_active());
    recent_mdec = 0;
    assert(!rb_fmv_media_active());
    assert(xa_queries == 0); /* audio decoding is independent of the video gate */
    puts("production FMV detector: XA music, silent video, and transitions PASS");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="psx-fmv-media-") as folder:
        source = Path(folder) / "media.c"
        exe = Path(folder) / "media.exe"
        source.write_text(fixture_source(), encoding="utf-8")
        subprocess.run([args.compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                        str(source), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
