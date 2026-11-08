# Android 17 mondrian — Qualcomm WFD Commonsys (AAC) source audit

**Product:** Independent PixelExperience-Reborn, official AOSP Android 17 as the sole platform base, POCO F5 Pro / Redmi K60 (`mondrian`, SM8475). Other custom-ROM trees are **read-only hardware references**. No LineageOS code or device trees were imported.

## 1. Source identity, scope and provenance

The archived PE `seventeen` and `fourteen` branches both point to **commit `7b125f664789e0ce2ee39e1beab8de5498af502a`** (2023-10-24), Git **tree `b4b92e707e39859c7c905bc077fe0d4a72bc39fb`**, eight Git-tree entries and seven files:
`.gitupstream`, `Android.mk`, `libaac/Android.mk`, `libaac/aacEncode.cpp/h`, `libaac/aacDecode.cpp/h`.

**Important:** this repository is only the vendor `libwfdaac_vendor` library, **not a standalone WFD/Miracast service**. It has no RTSP negotiation, HDCP implementation, MediaCodec/Codec2 video encode, `WfdService`, SurfaceFlinger, virtual display, service `init.rc`, VINTF or SELinux policy. Consequently no change here, even a passing source test, can prove that Miracast or screen recording has stopped rebooting/lagging.

Historical `libaac/Android.mk` uses a vendor shared library, static `libFraunhoferAAC`, old `external/aac/libAACenc/include` and `libAACdec/include`, `vendor.display.config@2.0`, `libdisplayconfig.qti`, `libavservices_minijail`, `libminijail`. It retains `-O0` and `LOCAL_SANITIZE := integer_overflow`. Those vendor display and media services/dependency paths are not established for our pinned Android17 source graph. The latest reference-only LineageOS `lineage-24.0` WFD commonsys uses `libaac/Android.bp`, newer `external/aac/cpp/` includes and fewer dependencies, but this does **not** license copying it into PE or guarantee Xiaomi vendor ABI compatibility.

## 2. Verified legacy bugs and actual PE source changes

### AAC encoder `libaac/aacEncode.cpp`

Previously `aacConfigure()` allocated `aacInfo` with `new`, never deleted it, and returned failure without closing a partially initialized encoder. It set only bitrate/sample rate and left mandatory channel-mode, AOT, transport and FDK init incomplete. `aacEncodeFrame()` ignored `n_inSize`, `n_outSize` and `p_length`; it only assigned the two descriptor buffer pointers without filling their counts, IDs, sizes, element sizes or sample counts. It could return **true with no valid encoded output**, or dereference a null `p_aacInfo` if configuration had not run.

Patch:
- Explicitly validates supported **PCM16 mono/stereo** and nonzero bitrate/sample rate, checks host `sizeof(INT_PCM)`; unsupported physical configurations fail rather than silently claim support.
- Creates a candidate FDK encoder, sets `AACENC_AOT=AOT_AAC_LC`, `AACENC_CHANNELMODE`, `AACENC_SAMPLERATE`, `AACENC_BITRATE`, `AACENC_TRANSMUX=TT_MP4_ADTS`, then finalizes through documented `aacEncEncode(handle, NULL, NULL, NULL, NULL)`.
- Preserves the existing valid encoder until a replacement is fully configured, closes it on success, and frees the old/new state correctly on all paths. The private C++ object's public fields and layout remain unchanged.
- Populates real `AACENC_BufDesc` arrays, `AACENC_InArgs::numInSamples`, `AACENC_OutArgs`, and validates input PCM16 size, channel alignment, integer ranges, reported bytes, and full input consumption. `*p_length` is zeroed on failure and set from checked `numOutBytes` on success. Zero bytes can mean correct internal FDK buffering and is not a complete AAC frame yet.
- The legacy method cannot report partially consumed PCM; it returns failure rather than pretending that all input was encoded. Correct multiframe streaming semantics still need verification against the actual vendor caller.

Official FDK encoder API is documented at https://android.googlesource.com/platform/external/aac/+/master/libAACenc/include/aacenc_lib.h ; the documented PCM input, encoded output, buffer IDs, sizes, initialization and output byte count are the basis for this change.

### AAC decoder `libaac/aacDecode.cpp`

Previously the decoder reused compressed input as the in-place PCM destination and passed `n_size / sizeof(INT_PCM)` as its PCM sample capacity without checking the decoded output format/length. Its configuration could leak a prior decoder if invoked again, and the `aacDecoder_SetParam` result was ignored.

Patch:
- Creates and validates a candidate decoder (channel count 1–6, positive sample rate, PCM sample width), checks parameter calls, closes failed candidates, and retains an existing working decoder on a failed reconfiguration.
- Allocates a separate **aligned temporary PCM array** (not aliasing compressed input); checks the actual decoded `CStreamInfo::frameSize`, `numChannels`, sample capacity and integer division before copying any PCM into the caller buffer.
- Rejects oversized input lengths **over 64 KiB** before allocation. All error paths release allocated scratch memory. The legacy API exposes only one pointer and one `n_size`, so an AAC frame whose output exceeds the supplied buffer **must fail**. This is a deliberate safety boundary. It is not a full redesign of a WFD streaming decoder; a real independent Android17 integration should use separate explicitly sized compressed-input and PCM-output buffers.
- Consult https://android.googlesource.com/platform/external/aac/+/master/libAACdec/include/aacdecoder_lib.h for official decoder output ownership and caller-provided capacity requirements.

### Product ownership

The old root `Android.mk` now wraps its entire archival QSSI/QMAA/low-RAM module selection in `ifneq ($(TARGET_DEVICE),mondrian)`. It **does not install this non-vetted codec wrapper on mondrian**, and leaves historical selection for all other devices unchanged. There is no unconditional global disabling of Miracast or Android platform codec APIs.

## 3. Hardware references and modern platform boundary

- LF52406 `mondrian_android_device_xiaomi_sm8450-common` lists proprietary **`system/framework/WfdCommon.jar`**, **`system_ext/bin/wfdservice64`**, `system_ext/priv-app/WfdService`, `vendor/bin/wfdvndservice`, `vendor/bin/wfdhdcphalservice`, HDCP DRM modules, WFD configs, and a wide set of `libwfd*` media/display/RTSP/codec blobs. Its `blob-patches/WfdCommon.patch` changes Java/Smali WFD listener compatibility. These are **read-only device-hardware indicators**, not PE dependencies automatically copied into this audit.
- The independent AOSP17 base must separately verify official MediaCodec/Codec2, AudioFlinger, graphics composer, BufferQueue/SurfaceFlinger/VDS, protected/unprotected video paths, Wi-Fi Direct and Wi-Fi/display policy. This standalone AAC repo cannot replace any of those layers.
- Before importing into PE device tree, verify by real **Xiaomi SM8475 vendor symbol consumer** whether `libwfdaac_vendor` is required at all, and whether old shared-library C++ ABI, old HIDL display link dependencies and actual FDK source include paths are supported. Never invent a library dependency, copy a LineageOS manifest or promote an archived 2018 media path to Android 17 just by renaming its makefile.
- For a device whose Miracast reboot was previously fixed with vendor configuration, **do not revert that known working workaround** without logs and targeted testing. Test source/sink sessions, `WfdCommon.jar` API, RTSP/HDCP, capture latency, thermal throttling, screen recording, protected content and graphics memory alongside physical device VTS/CTS checks.

## 4. Reproducible testing and limitations

`tests/check_android17_mondrian_wfdaac.py` runs the **actual** `aacEncode.cpp` and `aacDecode.cpp` with `g++ -std=c++17 -Wall -Wextra -Werror -pedantic`, AddressSanitizer and UBSan. The fake FDK API test doubles under `tests/fakes` **do not implement AAC encoding/decoding**; they inject allocation, param/config, short/invalid buffer, output length, reconfiguration and incomplete-input failures. `GNU Make` executes the real `Android.mk` against mondrian and reference QSSI/QMAA/low-RAM configurations. Tests explicitly verify no old WFD module activated on mondrian.

Separate manifest guard forbids this unverified root repo from the AOSP17 device-independent source graph by **path and historical repo aliases** until deliberate pinning, vendor ABI evidence, Android17 Soong compile and physical device tests exist. The checked-in `default.xml` remains unchanged.

**Status:** source bugs fixed, native host/CI checks, isolation guard and audit. **Not** an Android17 build, real libFraunhoferAAC encode/decode roundtrip, accurate real-time sync, HDCP, Wi-Fi display stack implementation, Soong/SELinux, Miracast connection or confirmed mondrian screen recording solution.
