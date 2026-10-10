# Known Issues

## Language Detection for Container Formats

For subtitles in container formats like MKV, MP4, etc, the language must be explicitly specified using the `--tesseract-lang` option. Automatic language detection from the subtitle stream is not reliable for container formats until ISO-639-2 support is added.

**Example:** specify the OCR language explicitly:
```bash
vobsub2srt --tesseract-lang eng movie.mkv
```

Supported language codes include: `eng`, `deu`, `fra`, `spa`, `ita`, `jpn`, `chi_sim`, `chi_tra`, and others supported by Tesseract, provided the language data is installed.

## IFO File Support

IFO file support is currently not functional. The program accepts the `-i` option for specifying IFO files for script compatibility, but this feature is not implemented in the current FFmpeg-based implementation.

## Musical Note Recognition

Musical notation and symbols are not properly recognized by the OCR engine. Subtitles containing musical notes, staff notation, or music-related symbols are misinterpreted or omitted entirely. Consider manual correction of subtitles containing musical notation.

---

*Last updated: 2026-10-10*
