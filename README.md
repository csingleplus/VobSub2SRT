# VobSub2SRT

VobSub2SRT is a simple command line program to convert DVD subtitles into SubRip `.srt` text subtitles by using OCR. It uses [FFmpeg](https://ffmpeg.org/) libraries for subtitle decoding and [Tesseract](https://github.com/tesseract-ocr/tesseract) for optical character recognition.

VobSub2SRT is released under the GPL3+ license.

The quality of the OCR depends on the text in the subtitles. You can correct mistakes in the `.srt` files with a text editor or a subtitle editor, or by using the `--blacklist` flag.

## Recent Updates

### October 2026
- All MPlayer dependencies (vobsub.c, spudec.c, mp_msg.c and headers) have been replaced with FFmpeg components
- Now uses `avformat_open_input()`, `avcodec_decode_subtitle2()`, and `AVSubtitle` handling
- Improved Tesseract integration
- New `--env` flag to show Tesseract's data path if TESSDATA_PREFIX is set.


### July 2026
- Added conditional libPNG header check inside vobsub2srt.c++ for compatibility with NixOS and others

### June 2026
- PNG export added
- Image inversion added (code by [Ethan Dye](https://github.com/ecdye/VobSub2SRT))
- Tesseract options added to improve accuracy

### April/May 2026
- Scaling of subtitles implemented
- Memory leak fixes in vobsub.c

### March 2026
- Memory safety improvements
- MPlayer verbosity separated from realtime text output

## Building

You need FFmpeg libraries, libPNG, Tesseract (minimum version 4), CMake and GCC to build `vobsub2srt`.

With a Debian-based distribution, you can install the dependencies with:

```bash
sudo apt-get install libpng-dev libtiff5-dev libtesseract-dev build-essential cmake pkg-config libavformat-dev libavcodec-dev libavutil-dev
```

You also need to install the Tesseract data for the languages you want to use:

```bash
sudo apt-get install tesseract-lang-eng
```

Then build with CMake:

```bash
cmake -B build
cmake --build build
sudo cmake --install build
```

Note again that support for Tesseract < 4.0 is deprecated.

### Building from NixOS (Tested with 26.05)
**NOTE: Version 2.0.0 currently not tested.**

```bash
nix-env -iA nixos.git nixos.gcc nixos.cmake nixos.tesseract nixos.libtiff nixos.libpng nixos.zlib nixos.ffmpeg
```

Then:

```bash
git clone https://github.com/csingleplus/VobSub2SRT && cd VobSub2SRT
cmake -B build
cmake --build build
sudo cmake --install build
```

This should install the program `vobsub2srt` to `/usr/local/bin`. Run configure with `-DCMAKE_INSTALL_PREFIX=<path>` for elsewhere. You can uninstall vobsub2srt with `sudo make uninstall`.

## Usage

VobSub2SRT converts DVD bitmap subtitles in either VobSub (`.idx` / `.sub`) format or directly from (unencrypted) VOB, MP4, MKV, etc, files into subtitles in `.srt` format. VobSub subtitles consist of two files called `Filename.idx` and `Filename.sub`. To convert subtitles simply call:

```bash
vobsub2srt Filename.extension 
```

with `Filename` being the file name of the subtitle files without the extension (on raw `.idx` / `.sub` files), or point to the media container your subtitles are in (with extension). VobSub2SRT writes the subtitles to a file called `Filename.srt`. You can see the subtitles as they are decoded with `--show`.

If a subtitle file contains more than one language you can use the `--lang` parameter to set the correct language (Use `--langlist` to find out about the languages in the file). For some languages you might need to set the Tesseract language yourself (e.g., chi_tra/chi_sim for traditional or simplified Chinese characters). You can use `--tesseract-lang` to do this. Multilingual subtitles may be decoded by combining langcodes as such: eng+fra. In most cases this should however be autodetected.

If you want to dump the subtitles as images (e.g. to check for correct OCR) you can use the `--dump-png` or `--dump-pgm` flag.

Use `--help` or read the manpage to get more information about the options of vobsub2srt.

## Bug Reports

Please submit bug reports or feature requests to the [issue tracker on GitHub](https://github.com/csingleplus/VobSub2SRT/issues).

If you have problems with a specific subtitle file then please check if it works in a media player (I recommend `mpv`) first.
```

For bug reports please run `vobsub2srt` with the `--verbose` option and copy and paste the lines around where the error occurs to the report, and describe the issue surrounding it. Please include the version used with the problem as well.
