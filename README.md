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

## Tune for Linux

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
