# libisac
This filter decodes iSAC, the wideband speech codec of WebRTC.

## Requirements

[CMake](https://cmake.org/) is used as a build system. To install it, follow
[Debian build instructions](developing_in_debian.md).

[Emscripten SDK](https://emscripten.org/) is required for building
WebAssembly artifacts. To install it, follow the
[Download and Install](https://emscripten.org/docs/getting_started/downloads.html)
guide:

```bash
cd $OPT

# Get the emsdk repo.
git clone https://github.com/emscripten-core/emsdk.git

# Enter that directory.
cd emsdk

# Download and install the latest SDK tools.
./emsdk install latest

# Make the "latest" SDK "active" for the current user. (writes ~/.emscripten file)
./emsdk activate latest
```

## Building the accessor

```bash
# Setup EMSDK and other environment variables. In practice EMSDK is set to be
# $OPT/emsdk.
source $OPT/emsdk/emsdk_env.sh

# Assuming you are in the root level of the cloned repo :
emcmake cmake .
emmake make
```

Once built, you can use and distribute libisac_1.wasm with your universal tags.

## Where the sources come from

iSAC was removed from WebRTC in 2022 and no distribution packages it, so there
is no library to build against. `lib/libisac.a` is built from a vendored
snapshot of the last revision where the codec and the signal processing
routines it calls still matched each other:

    https://github.com/webrtc-sdk/webrtc
    branch main (m93_release), commit 9ea05f163f315b74facf9cf98f4f68f793e286e5

That pairing is why a revision is pinned rather than the newest tree that still
has the files: `WebRtcSpl_AnalysisQMF` was later changed from int16 to float
while `isac/main` kept calling the int16 form, and a checkout that mixes the
two compiles with warnings and decodes to noise.

## The file format

There is none, properly speaking: iSAC only ever travelled over RTP. The one
container that exists is the bitstream dump WebRTC's own test program writes
and reads — per frame, a big-endian 16-bit length then the payload, with no
magic and no header.

Nothing in it says whether the stream is 16 kHz wideband or 32 kHz
super-wideband, so `srate` is an option, defaulting to 16000.

## Documentation

For more details, please visit our documentation at https://bevara.com/documentation/develop/.
