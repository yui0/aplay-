# 🎶 **aplay+**: A Simple and High-Quality Audio Player

![GitHub Repo stars](https://img.shields.io/github/stars/yui0/aplay-?style=social)
![GitHub code size in bytes](https://img.shields.io/github/languages/code-size/yui0/aplay-)
![Languages](https://img.shields.io/github/languages/top/yui0/aplay-)
[![GitHub release (latest by date)](https://img.shields.io/github/v/release/yui0/aplay-)](https://github.com/yui0/aplay-/releases)
[![MIT License](https://img.shields.io/badge/license-MIT-blue.svg?style=flat)](LICENSE)

🎧 **Enjoy BitPerfect audio playback with simplicity and precision!**

![Logo](logo.jpeg)

## 💿 Supported File Formats
aplay+ supports a variety of popular audio formats:
- 🌟 **FLAC**: High-quality lossless compression
- 🌟 **DSD (DSF)**: Direct Stream Digital (high-resolution 1-bit audio)
- 🌊 **WAV**: Uncompressed audio with crystal-clear quality
- 🎶 **MP3**: The most commonly used compressed format
- 🎵 **Ogg Vorbis**: Great compression with excellent sound
- 📱 **AAC (mp4/m4a)**: Widely used in iPhones and YouTube

## 🔧 How to build

### Build Online
- Build easily with Google Colab:
- [Build with Colab](aplay%2B.ipynb) &nbsp;&nbsp; <a href="https://colab.research.google.com/github/yui0/aplay-/blob/master/aplay%2B.ipynb" target="_parent"><img src="https://colab.research.google.com/assets/colab-badge.svg" alt="Open In Colab"/></a>

### Build Locally
1. Install required libraries:
  ```bash
  # dnf install alsa-lib-devel
  $ make
  ```

2. Clone the repository and build:
  ```bash
  git clone https://github.com/yui0/aplay-.git
  cd aplay-
  make
  ```

## 🌸 How to use

### Basic Commands
```bash
$ ./aplay+ -h
Usage: ./aplay+ [options] dir

Options:
-h                 Print this help message
-d <device name>   Specify ALSA device [e.g., default hw:0,0 plughw:0,0...]
-f                 Use 32-bit floating-point playback
-r                 Recursively search directories
-x                 Enable random playback
-s <regexp>        Search files with a regex
-t <ext type>      Specify file type (e.g., flac, mp3, wma...)
-p                 Optimize for Linux platforms
```

### Examples
- 🔀 **Random playback**:
  ```bash
  $ ./aplay+ -rx .
  ```
- 🎤 **Search for a specific artist**:
  ```bash
  $ ./aplay+ -rx -d hw:7,0 /Music/ -s ZARD
  ```
- 🎹 **Exclude instrumentals from playback**:
  ```bash
  $ ./aplay+ -rfx -d hw:7,0 /Music/ -s '^(?!.*nstrumental).*$'
  ```

## 🌟 Linux Optimization Settings

### 🚀 Optimize Disk I/O
Add the following to your `sysctl.conf`:
```conf
vm.dirty_ratio = 40
vm.dirty_background_ratio = 10
vm.dirty_expire_centisecs = 3000
vm.dirty_writeback_centisecs = 500
#dev.hpet.max-user-freq = 3072
vm.overcommit_memory = 1
```
Apply changes:
```bash
sysctl -p
```

### ⚙️ Adjust Scheduler Settings
Optimize SSDs and HDDs with the following script:
```bash
#!/bin/sh
#cat /sys/block/sd*/queue/scheduler
for FILE in /sys/block/sd*/queue/scheduler
do
	[ -f $FILE ] || continue
	echo -n none > $FILE
done
```

### 💨 Set CPU Performance Mode
Use this script to switch CPU governor to "performance":
```bash
#!/bin/sh
#cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
for CPUFREQ in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
do
	[ -f $CPUFREQ ] || continue
	echo -n performance > $CPUFREQ
done
```

### I/O scheduler
```60-ioschedulers.rules
# scheduler for non rotational, SSD
ACTION=="add|change", KERNEL=="sd[a-z]|mmcblk[0-9]*", ATTR{queue/rotational}=="0", ATTR{queue/scheduler}="none"
# scheduler for rotational, HDD
ACTION=="add|change", KERNEL=="sd[a-z]", ATTR{queue/rotational}=="1", ATTR{queue/scheduler}="bfq"
```

fstrim -v /

### Timer

```
#cat /sys/devices/system/clocksource/clocksource0/current_clocksource
echo tsc > /sys/devices/system/clocksource/clocksource0/current_clocksource
```

ulimit -a

## 🎶 Sample Music

- https://www.nativedsd.com/dsd-reviews/homeland-pure-dsd256-large-orchestra-recording-from-eudora-records/
- https://www.iriver.jp/products/product_94.php#5
- https://samplerateconverter.com/educational/dsd1024
- https://www.hifistatement.net/download/item/2227-pcm-384-32-dsd64-dsd128-und-dsd-256-a-trace-of-grace

## 📖 References

- [stb](https://github.com/nothings/stb)
- [dr_libs](https://github.com/mackron/dr_libs)
- [mini_al](https://github.com/dr-soft/mini_al)
- [minilibs/regex](https://github.com/ccxvii/minilibs)
- [parg](https://github.com/jibsen/parg)
- [Related Blog Posts](https://pulseaudio.blog.fc2.com/blog-entry-1.html)
- https://kazuhira-r.hatenablog.com/entry/2021/05/22/210532
- https://github.com/nothings/single_file_libs

🎵 **Experience perfect audio playback with aplay+! Start your music journey today!**
