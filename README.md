# aplay+

![GitHub Repo stars](https://img.shields.io/github/stars/yui0/aplay-?style=social)
![GitHub code size in bytes](https://img.shields.io/github/languages/code-size/yui0/aplay-)
![Lines of code](https://img.shields.io/tokei/lines/github/yui0/aplay-)
[![GitHub release (latest by date)](https://img.shields.io/github/v/release/yui0/aplay-)](https://github.com/yui0/aplay-/releases)
[![MIT License](https://img.shields.io/badge/license-MIT-blue.svg?style=flat)](LICENSE)

a simple BitPerfect player

## Supported file formats

- FLAC (Free Lossless Audio Codec)
- WAV (RIFF waveform Audio Format)
- MP3 (MPEG-1 Audio Layer-3)
- Ogg Vorbis
- AAC (mp4/m4a)

## How to build

- [build with Colab](aplay%2B.ipynb) &nbsp;&nbsp; <a href="https://colab.research.google.com/github/yui0/aplay-/blob/master/aplay%2B.ipynb" target="_parent"><img src="https://colab.research.google.com/assets/colab-badge.svg" alt="Open In Colab"/></a>

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
-p                 Tuning for Linux platforms

$ ./aplay+ -rx .
$ ./aplay+ -rx -d hw:7,0 /Music/ -s ZARD
$ ./aplay+ -rfx -d hw:7,0 /Music/ -s '^(?!.*nstrumental).*$'
```

## Tuning for Linux platforms

* Disk I/O

```sysctl.conf
vm.dirty_ratio = 40
vm.dirty_background_ratio = 10
vm.dirty_expire_centisecs = 3000
vm.dirty_writeback_centisecs = 500

#dev.hpet.max-user-freq = 3072

vm.overcommit_memory = 1
```

sysctl -p

```
#!/bin/sh
#cat /sys/block/sd*/queue/scheduler
for FILE in /sys/block/sd*/queue/scheduler
do
	[ -f $FILE ] || continue
	echo -n none > $FILE
done
```

```60-ioschedulers.rules
# scheduler for non rotational, SSD
ACTION=="add|change", KERNEL=="sd[a-z]|mmcblk[0-9]*", ATTR{queue/rotational}=="0", ATTR{queue/scheduler}="none"
# scheduler for rotational, HDD
ACTION=="add|change", KERNEL=="sd[a-z]", ATTR{queue/rotational}=="1", ATTR{queue/scheduler}="bfq"
```

fstrim -v /

* Setting CPU clock to performance

```
#!/bin/sh
#cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
for CPUFREQ in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
do
	[ -f $CPUFREQ ] || continue
	echo -n performance > $CPUFREQ
done
```

* Timer

```
#cat /sys/devices/system/clocksource/clocksource0/current_clocksource
echo tsc > /sys/devices/system/clocksource/clocksource0/current_clocksource
```

ulimit -a

## References

- https://github.com/nothings/stb
- https://github.com/mackron/dr_libs
- https://github.com/dr-soft/mini_al
- https://github.com/ccxvii/minilibs
- https://github.com/jibsen/parg
- https://pulseaudio.blog.fc2.com/blog-entry-1.html
- https://kazuhira-r.hatenablog.com/entry/2021/05/22/210532
