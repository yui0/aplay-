# aplay+

a simple BitPerfect player

## Supported file formats

- FLAC (Free Lossless Audio Codec)
- WAV (RIFF waveform Audio Format)
- MP3 (MPEG-1 Audio Layer-3)
- Ogg Vorbis
- AAC (mp4/m4a)

## How to build

```bash
# dnf install alsa-lib-devel
$ make

```

## How to use

```bash
$ make
$ ./aplay+ -h
Usage: ./aplay+ [options] dir

Options:
-h                 Print this message
-d <device name>   ALSA device name [default hw:0,0 plughw:0,0...]
-f                 Use 32bit floating
-r                 Recursively search for directory
-x                 Random play
-s <regexp>        Search files
-t <ext type>      File type [flac mp3 wma...]

$ ./aplay+ -rx .
$ ./aplay+ -rx -d hw:7,0 /Music/ -s ZARD
$ ./aplay+ -rfx -d hw:7,0 /Music/ -s '^(?!.*nstrumental).*$'
```

## References

- https://github.com/nothings/stb
- https://github.com/mackron/dr_libs
- https://github.com/dr-soft/mini_al
- https://github.com/ccxvii/minilibs
- https://github.com/jibsen/parg
