#!/usr/bin/env python3
"""Prepare a generic POV-Ray 1.0 copy without modifying the historical port."""
import pathlib
import os
import re
import shutil
import sys

root = pathlib.Path(__file__).resolve().parents[4]
work = pathlib.Path(sys.argv[1]).resolve()
source = root / "transputer/t4/examples/povray/povsrc/source"
output = os.environ.get("POV_OUTPUT", "row")
if output not in ("row", "pixel"):
    raise SystemExit("POV_OUTPUT must be row or pixel")
dest = work / "source"
dest.mkdir(parents=True, exist_ok=True)
for item in source.iterdir():
    if item.suffix in (".c", ".h"):
        shutil.copyfile(item, dest / item.name)

# Match the historical Transputer port's tolerance.  Keep the numerical
# source expressions and all source copyright notices intact.
(dest / "config.h").write_text("""#include <stdlib.h>
#include <time.h>
#define EPSILON 1.0e-5
#define PARAMS(x) x
#define START_TIME
#define STOP_TIME
#define TIME_ELAPSED 0.0;
#define STARTUP_POVRAY
#include <math.h>
#define sqrt(x) __fpsqrt(x)
""")
txtmap = (dest / "txtmap.c").read_text()
txtmap, count = re.subn(
    r"^extern (void|int|DBL) "
    r"(cylindrical_image_map|torus_image_map|spherical_image_map|planar_image_map|"
    r"no_interpolation|bilinear|norm_dist|Interp|image_colour_at) ",
    r"static \1 \2 ", txtmap, flags=re.M)
assert count == 9, "historical txtmap declarations changed"
(dest / "txtmap.c").write_text(txtmap)
(dest / "display.c").write_text("""#include <stdio.h>
#include <stdlib.h>
static FILE *image;
void display_init(int width, int height) {
    image = fopen("image.ppm", "wb");
    if (!image) exit(1);
    fprintf(image, "P6\\n%d %d\\n255\\n", width, height);
}
void display_plot(int x, int y, char r, char g, char b) {
    unsigned char pixel[3] = {(unsigned char)r, (unsigned char)g, (unsigned char)b};
    (void)x; (void)y;
    if (fwrite(pixel, 1, 3, image) != 3) exit(1);
}
void display_finished(void) {
    if (image) { if (fclose(image)) exit(1); image = 0; }
}
void display_close(void) { display_finished(); }
""")
if output == "row":
    shutil.copyfile(pathlib.Path(__file__).with_name("display-row.c"), dest / "display.c")
    shutil.copyfile(root / "transputer/t4/examples/povray/povsrc/machine/transp/rgbrow.h",
                    dest / "rgbrow.h")
boot = (root / "transputer/tests/test_boot.S").read_text()
old = "0x24, 0x20, 0x20, 0x20, 0xB0"
assert boot.count(old) == 1
# AJW 0x200000 words places workspace 8 MiB above its initial value,
# above the complete renderer, library and BSS, below the 16 MiB heap.
boot = boot.replace(old, "0x22, 0x20, 0x20, 0x20, 0x20, 0xB0")
boot = boot.replace("Use AJW +0x4000 words = +0x10000 bytes (64 KiB)",
                    "Use AJW +0x200000 words = +0x800000 bytes (8 MiB)")
boot = boot.replace("/* ajw 0x4000 */", "/* ajw 0x200000 */")
(work / "boot.S").write_text(boot)
