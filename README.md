# VobSub2SRT

VobSub2SRT is a simple command line program to convert DVD subtitles into SubRip `.srt` text subtitles by using OCR. It uses [FFmpeg](https://ffmpeg.org/) libraries for subtitle decoding and [Tesseract](https://github.com/tesseract-ocr/tesseract) for optical character recognition.

VobSub2SRT is released under the GPL3+ license.

The quality of the OCR depends on the text in the subtitles and the type of Tesseract data you have. You can correct mistakes in the `.srt` files with a text editor or a subtitle editor, or by using the `--blacklist` flag. See below to download more accurate Tesseract data than your distro may provide in its packaging.

## Recent Updates

### November 2026
- Simplified extension handling by not handling it at all. See the "Usage" section of this file.
- ~~Fixed behavior when invoking without an extension for `.idx/.sub` files.~~
- Beginning to separate code out of main entrypoint.

### October
- Preprocessing added with `libavfilter` making use of the pad, super2xsai and unsharp filters, improving accuracy.
- All MPlayer dependencies (vobsub.c, spudec.c, mp_msg.c and headers) have been replaced with FFmpeg components
- Now uses `avformat_open_input()`, `avcodec_decode_subtitle2()`, and `AVSubtitle` handling
- Implemented pre-processing with Super2xSaI and unsharp (rc4).
- Improved Tesseract integration
- New `--env` flag to show Tesseract's data path if TESSDATA_PREFIX is set.
- Use the `--output`, or `-o` flag to set your own filename. NOTE: I forgot to run git add on rc3 and broke options. This is now fixed in rc4.
- `--ifo` flag fixed (rc4).
- Segfault when using `--lang` and an undefined langcode in an .idx fixed (rc4).


## Building

You need FFmpeg libraries, libPNG, Tesseract (minimum version 4), CMake and GCC to build `vobsub2srt`.

With a Debian-based distribution, you can install the dependencies with:

```bash
sudo apt-get install libpng-dev libtiff5-dev libtesseract-dev build-essential cmake pkg-config libavfilter-dev libavformat-dev libavcodec-dev libavutil-dev
```

You also need to install the Tesseract data for the languages you want to use:

```bash
sudo apt-get install tesseract-lang-eng
```

Note: Tests with Debian, Ubuntu, and Fedora all ship tessdata_fast, which is optimized for speed instead of accuracy, so you may wish to download [tessdata_best](https://github.com/tesseract-ocr/tessdata_best) and either point `--tesseract-data` to its directory or set the TESSDATA_PREFIX variable for the most accurate results. Info [here](https://tesseract-ocr.github.io/tessdoc/Data-Files.html).

Arch Linux ships standard tessdata, which is `tessdata_best` with the legacy engine added.


Then build with either:

```bash
./configure
make
sudo make install
```

Or:

```bash
cmake -B build
cmake --build build
sudo cmake --install build
```

Tesseract 4.0 or newer is required. For systems that only have Tesseract 3, the legacy branch (`git checkout legacy`) still builds, but it is unmaintained apart from fixes needed to keep it compiling. Bug reports and feature requests are only accepted for the main branch, except in the case legacy stops building and/or producing output.


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

with `Filename` being the file name of the subtitle files (specify the `.idx` file if converting `.idx/.sub` files), or point to the media container your subtitles are in (e.g. a VOB, MP4, MKV, etc). VobSub2SRT writes the subtitles to a file called `Filename.langcode.srt`, or if using the `--output` argument, a specified filename. You can see the subtitles as they are decoded with `--show`.

If a subtitle file contains more than one language you can use the `--lang` parameter to set the correct language (Use `--langlist` to find the languages in the file). For some languages you might need to set the Tesseract language yourself (e.g., chi_tra/chi_sim for traditional or simplified Chinese characters). Subtitles without a language code in their metadata default to the `eng` language code, otherwise it is automatically detected. You can use `--tesseract-lang` to override this. Multilingual subtitles may be decoded by combining langcodes as such: `--tesseract-lang eng+fra`.

If you want to dump the subtitles as images (e.g. to check for correct OCR) you can use the `--dump-png` or `--dump-pgm` flag. This writes sequentially-named images such as `Filename.eng-0001.png` with the number incrementing by subtitle count.

Use `--help` or read the manpage to get more information about the options of vobsub2srt.


## Bug Reports

Please submit bug reports or feature requests to the [issue tracker on GitHub](https://github.com/csingleplus/VobSub2SRT/issues).

If you have problems with a specific subtitle file then please check if it works in a media player (I recommend `mpv`) first.


For bug reports please run `vobsub2srt` with the `--verbose` option and copy and paste the lines around where the error occurs to the report, and describe the issue surrounding it. Please include the version used with the problem as well.
