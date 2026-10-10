"""Exercise production LightFor with directional lighting enabled and disabled."""

import argparse
import re
import subprocess
import tempfile
from pathlib import Path
from voxel_runtime_test import function, ROOT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    voxel = (ROOT / "src/voxel/ctr_voxel.c").read_text()
    light_type = re.search(
        r"typedef struct\s*\{\s*float sun\[3\], shade\[3\];.*?\}\s*VoxelLight;",
        voxel,
        re.S,
    ).group()
    constants = "\n".join(re.findall(r"^#define VOXEL_HAZE_\w+.*$", voxel, re.M))
    source = r"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "voxel_world.h"
static bool enabled=true, outdoor=true;
static unsigned clockCalls;
static bool CtrGame_IsOutdoor(void) { return outdoor; }
static int CtrSettings_GetInt(const char *key,int fallback) {
    return !strcmp(key,"day_night") ? enabled : fallback;
}
static void CtrDayNight_Tint(float tint[3]) {
    ++clockCalls;tint[0]=0.52f;tint[1]=0.60f;tint[2]=0.82f;
}
#if CTR_VOXEL_LIGHTING
VoxelWeatherClass VoxelWorld_Weather(void) { return VOXEL_WEATHER_CLEAR; }
bool VoxelWorld_Underground(void) { return false; }
float VoxelWorld_FogDensity(void) { return 0; }
#endif
"""
    source += constants + "\n" + light_type + "\n#if CTR_VOXEL_LIGHTING\n"
    source += function(voxel, "static VoxelLight LightMix(")
    source += function(voxel, "static VoxelLight FogLight(") + "\n#endif\n"
    source += function(voxel, "static VoxelLight LightFor(")
    source += r"""
int main(void) {
    enabled=false;
    VoxelLight day=LightFor(false);
    assert(clockCalls==0);
    enabled=true;
    VoxelLight night=LightFor(false);
    assert(clockCalls==1);
    const float tint[3]={0.52f,0.60f,0.82f};
    for(unsigned c=0;c<3;++c) {
        assert(fabsf(night.sun[c]-day.sun[c]*tint[c])<0.00001f);
        assert(fabsf(night.shade[c]-day.shade[c]*tint[c])<0.00001f);
    }
    VoxelLight indoors=LightFor(true);
    assert(clockCalls==1);
    for(unsigned c=0;c<3;++c) assert(indoors.sun[c]==1 && indoors.shade[c]==1);
    outdoor=false;
    VoxelLight cave=LightFor(false);
    assert(clockCalls==1);
    for(unsigned c=0;c<3;++c) assert(cave.sun[c]==day.sun[c]);
#if !CTR_VOXEL_LIGHTING
    for(unsigned c=0;c<3;++c) assert(day.sun[c]==1 && day.shade[c]==1);
    assert(night.haze==0 && night.rays==0 && night.motes==0);
#endif
    printf("PASS voxel clock tint: lighting=%d, outdoors, option off, interiors/caves\n",CTR_VOXEL_LIGHTING);
}
"""
    with tempfile.TemporaryDirectory(prefix="voxel-clock-", dir=ROOT / "build") as tmp:
        path = Path(tmp) / "test.c"
        path.write_text(source)
        for lighting in (0, 1):
            exe = Path(tmp) / f"test-{lighting}.exe"
            subprocess.run(
                [
                    args.cc,
                    "-std=c99",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-DCTR_VOXEL_LIGHTING={lighting}",
                    "-I" + str(ROOT / "src/voxel"),
                    str(path),
                    "-lm",
                    "-o",
                    str(exe),
                ],
                check=True,
            )
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
