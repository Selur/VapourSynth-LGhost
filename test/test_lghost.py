#!/usr/bin/env python3
"""Functional tests for the LGhost VapourSynth plugin.

Usage: python3 test/test_lghost.py [path/to/liblghost.so]

Without an argument the plugin is expected to be autoloaded (e.g. from the
installed wheel) or to be found in site-packages/vapoursynth/plugins.

Requires the vapoursynth Python module (R55 or newer).
"""
import glob
import hashlib
import os
import sys

import vapoursynth as vs

core = vs.core


def load_plugin(args):
    if args:
        core.std.LoadPlugin(args[0])
        return
    if hasattr(core, "lghost"):
        return
    # Wheel layout: site-packages/vapoursynth/plugins/<plugin>
    plugin_dir = os.path.join(os.path.dirname(vs.__file__), "plugins")
    for ext in ("*.so", "*.dll", "*.dylib"):
        for path in glob.glob(os.path.join(plugin_dir, "*lghost" + ext)):
            core.std.LoadPlugin(path)
            return
    raise SystemExit("FAIL: lghost plugin is neither autoloaded nor found in " + plugin_dir)


def check(cond, msg):
    if not cond:
        raise SystemExit("FAIL: " + msg)
    print("ok  ", msg)


def expect_error(fn, msg):
    try:
        fn()
    except vs.Error as e:
        check(True, "%s (%s)" % (msg, str(e).strip().splitlines()[-1]))
    else:
        raise SystemExit("FAIL: no error raised: " + msg)


def line_clip(fmt, value, width=64, height=8, x=20):
    """A black frame with one vertical line of `value` at column x."""
    black = core.std.BlankClip(width=width, height=height, format=fmt, length=1, color=[0] * fmt.num_planes)
    line = core.std.BlankClip(width=1, height=height, format=fmt, length=1, color=[value] * fmt.num_planes)
    left = core.std.Crop(black, right=width - x)
    right = core.std.Crop(black, left=x + 1)
    return core.std.StackHorizontal([left, line, right])


def textured(width, height, frames=2):
    """Smooth gradients with edges in every direction, different per frame, built from coloured tiles."""
    out = []
    for n in range(frames):
        rows = []
        for y in range(5):
            tiles = [core.std.BlankClip(width=8, height=8, format=vs.YUV444P8, length=1,
                                        color=[(x * 53 + y * 97 + n * 31) % 256, (x * 29 + 128) % 256, (y * 71 + 64) % 256])
                     for x in range(7)]
            rows.append(core.std.StackHorizontal(tiles))
        out.append(core.std.StackVertical(rows).resize.Bicubic(width, height, format=vs.YUV420P8))
    return core.std.Splice(out)


def row(clip):
    f = clip.get_frame(0)
    return [f[0][2, x] for x in range(f.width)]


def digest(clip):
    h = hashlib.sha1()
    for n in range(clip.num_frames):
        f = clip.get_frame(n)
        for p in range(clip.format.num_planes):
            h.update(bytes(f[p]))
    return h.hexdigest()


def main():
    load_plugin(sys.argv[1:])
    check(hasattr(core, "lghost"), "plugin loaded (namespace lghost)")

    for fmt, value in ((vs.GRAY8, 200), (vs.GRAY16, 51200), (vs.GRAYS, 0.8)):
        fmt = core.get_video_format(fmt)
        out = row(core.lghost.LGhost(line_clip(fmt, value), mode=[2], shift=[4], intensity=[64], opt=1))
        expected = value // 2 if fmt.sample_type == vs.INTEGER else value / 2
        others = [v for x, v in enumerate(out) if x not in (20, 24)]
        check(abs(out[20] - value) < 1e-6 and abs(out[24] - expected) < 1e-6 and not any(others),
              "%s mode 2: ghost of half the line value 4 pixels to the right" % fmt.name)

    # All code paths give the same result where the CPU has them (opt 2-4 fall back to C when not available).
    src = textured(202, 38)
    for fmt in (vs.YUV420P8, vs.YUV420P16, vs.YUV444PS):
        clip = src.resize.Point(format=fmt)
        args = dict(mode=[1, 2, 3, 4], shift=[3, -5, 7, -2], intensity=[40, -20, 90, -100])
        ref = digest(core.lghost.LGhost(clip, opt=1, **args))
        same = [digest(core.lghost.LGhost(clip, opt=o, **args)) == ref for o in (0, 2, 3, 4)]
        if fmt == vs.YUV444PS:
            # Float: the SIMD paths use fused multiply-add, compare with a tolerance instead.
            a = core.lghost.LGhost(clip, opt=1, **args)
            b = core.lghost.LGhost(clip, opt=0, **args)
            diff = core.std.PlaneStats(core.std.Expr([a, b], "x y - abs"), plane=0).get_frame(0).props["PlaneStatsMax"]
            check(diff < 1e-5, "%s: C and SIMD path within 1e-5 (max %g)" % (core.get_video_format(fmt).name, diff))
        else:
            check(all(same), "%s: opt 0-4 bit-identical to the C path" % core.get_video_format(fmt).name)

    clip = core.std.BlankClip(width=64, height=8, format=vs.YUV420P8, length=1)
    expect_error(lambda: core.lghost.LGhost(core.std.BlankClip(format=vs.YUV420P10).resize.Point(format=vs.YUV420PH), mode=[1], shift=[1], intensity=[1]),
                 "half float rejected")
    expect_error(lambda: core.lghost.LGhost(clip, mode=[5], shift=[1], intensity=[1]), "mode 5 rejected")
    expect_error(lambda: core.lghost.LGhost(clip, mode=[1], shift=[0], intensity=[1]), "shift 0 rejected for mode 1")
    expect_error(lambda: core.lghost.LGhost(clip, mode=[1], shift=[1], intensity=[0]), "intensity 0 rejected")
    expect_error(lambda: core.lghost.LGhost(clip, mode=[1, 2], shift=[1], intensity=[1]), "element count mismatch rejected")
    expect_error(lambda: core.lghost.LGhost(clip, mode=[1], shift=[1], intensity=[1], planes=[0, 0]), "plane twice rejected")
    expect_error(lambda: core.lghost.LGhost(clip, mode=[1], shift=[1], intensity=[1], opt=5), "opt 5 rejected")

    print("all tests passed")


if __name__ == "__main__":
    main()
