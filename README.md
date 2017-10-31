# aplay+

a simple BitPerfect player

## Supported file formats

- FLAC (Free Lossless Audio Codec)
- WAV (RIFF waveform Audio Format)
- MP3 (MPEG-1 Audio Layer-3)
- Ogg Vorbis

## How to use

```bash
$ make
$ ./aplay+ -h
Usage: ./aplay+ [options] dir

Options:
-d <device name>   ALSA device name [default hw:0,0 plughw:0,0...]
-h                 Print this message
-r                 Recursively search for directory
-x                 Random play

$ ./aplay+ -rx .
```

## References

- https://github.com/mackron/dr_libs
- https://github.com/dr-soft/mini_al
