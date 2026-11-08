#!/usr/bin/env python3
"""Host-only source checks for archived Qualcomm WFD AAC on AOSP17 mondrian.

No actual Miracast negotiation, SurfaceFlinger/VDS, Wi-Fi Direct, codec binary
ABI, video/audio stream, HDCP, physical device or Android17 Soong build.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WfdAacSourceTests(unittest.TestCase):
    def test_actual_encoder_source_is_not_a_success_stub(self):
        src = (ROOT / "libaac/aacEncode.cpp").read_text(encoding="utf-8")
        for symbol in (
            "AACENC_CHANNELMODE", "AACENC_TRANSMUX", "AACENC_AOT",
            "IN_AUDIO_DATA", "OUT_BITSTREAM_DATA",
            "numInSamples", "numOutBytes",
            "*p_length = 0", "aacEncClose(&encoder)",
            "new (std::nothrow) aacInfo{}",
        ):
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, src)
        self.assertNotIn("(void)n_inSize;", src)
        self.assertNotIn("(void)n_outSize;", src)
        self.assertNotIn("(void)p_length;", src)
        self.assertIn("static_cast<unsigned int>(state->outArg.numOutBytes) > n_outSize", src)

    def test_real_cxx_encoder_under_memory_sanitizers(self):
        with tempfile.TemporaryDirectory(prefix="pe-wfdaac-") as td:
            exe = Path(td) / "wfdaac-encode-test"
            cmd = [
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
                "-I", str(ROOT / "tests/fakes"),
                "-I", str(ROOT / "libaac"),
                str(ROOT / "libaac/aacEncode.cpp"),
                str(ROOT / "tests/wfdaac_encoder_host.cpp"),
                "-o", str(exe),
            ]
            build = subprocess.run(cmd, capture_output=True, text=True,
                                   cwd=ROOT, check=False)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(exe)], capture_output=True, text=True, cwd=ROOT,
                check=False, env={
                    **os.environ,
                    "ASAN_OPTIONS": "alloc_dealloc_mismatch=1:detect_leaks=1",
                    "UBSAN_OPTIONS": "halt_on_error=1",
                },
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("PASS: WFD AAC configure", run.stdout)

    def product_make(self, device, board="taro", qmaa="false",
                     qmaa_override="false", low_ram="false"):
        with tempfile.TemporaryDirectory(prefix="pe-wfd-make-") as td:
            tmp = Path(td)
            child = tmp / "included.mk"
            child.write_text("WFD_CHILD_INCLUDED := true\n", encoding="utf-8")
            mk = tmp / "check.mk"
            mk.write_text(
                "my-dir = " + str(ROOT) + "\n"
                "is-board-platform = false\n"
                "is-board-platform-in-list = false\n"
                "all-makefiles-under = " + str(child) + "\n"
                "WFD_CHILD_INCLUDED := false\n"
                "include " + str(ROOT / "Android.mk") + "\n"
                ".PHONY: check\ncheck:\n"
                "\t@echo CHILD=$(WFD_CHILD_INCLUDED)\n"
                "\t@echo LOCAL_PATH=$(LOCAL_PATH)\n",
                encoding="utf-8",
            )
            cmd = [
                "make", "--no-print-directory", "-f", str(mk), "check",
                "TARGET_DEVICE=" + device,
                "TARGET_BOARD_PLATFORM=" + board,
                "QSSI_SUPPORTED_PLATFORMS=taro",
                "TARGET_USES_QMAA=" + qmaa,
                "TARGET_USES_QMAA_OVERRIDE_WFD=" + qmaa_override,
                "TARGET_HAS_LOW_RAM=" + low_ram,
                "TARGET_BOARD_SUFFIX=",
            ]
            run = subprocess.run(cmd, capture_output=True, text=True,
                                 cwd=ROOT, check=False)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            return run.stdout

    def test_mondrian_no_archived_aac_in_all_qssi_build_modes(self):
        for qmaa, override in (("false", "false"),
                               ("true", "false"),
                               ("true", "true")):
            with self.subTest(qmaa=qmaa, override=override):
                self.assertIn("CHILD=false", self.product_make(
                    "mondrian", qmaa=qmaa, qmaa_override=override))

    def test_non_mondrian_historical_selection_unchanged(self):
        self.assertIn("CHILD=true", self.product_make("historical_target"))
        self.assertIn("CHILD=false", self.product_make(
            "historical_target", qmaa="true"))
        self.assertIn("CHILD=true", self.product_make(
            "historical_target", qmaa="true", qmaa_override="true"))
        self.assertIn("CHILD=false", self.product_make(
            "historical_target", low_ram="true"))
        self.assertIn("CHILD=false", self.product_make(
            "historical_target", board="kalama"))

    def test_archive_contains_no_rendering_or_wfd_service(self):
        archived = {"Android.mk", ".gitupstream", "libaac/Android.mk",
                    "libaac/aacDecode.cpp", "libaac/aacDecode.h",
                    "libaac/aacEncode.cpp", "libaac/aacEncode.h"}
        for name in archived:
            self.assertTrue((ROOT / name).is_file(), name)
        makefile = (ROOT / "libaac/Android.mk").read_text(encoding="utf-8")
        self.assertIn("LOCAL_MODULE := libwfdaac_vendor", makefile)
        self.assertIn("libFraunhoferAAC", makefile)
        self.assertIn("libdisplayconfig.qti", makefile)
        self.assertIn("vendor.display.config@2.0", makefile)
        self.assertFalse(list(ROOT.rglob("*.rc")))
        self.assertFalse(list(ROOT.rglob("*.xml")))


if __name__ == "__main__":
    unittest.main(verbosity=2)
