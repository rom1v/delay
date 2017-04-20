`delay` is a shell command introducing a constant delay between its standard
input and its standard output.

## Usage

    delay [-b <dtbufsize>] <delay>

`dtbufsize` is the buffer size storing the data until it has been written to
stdout, in bytes.

The following modifiers are accepted:
 * `12k` means 12Kb (12×1024)
 * `12m` means 12Mb (12×1024²)
 * `12g` means 12Gb (12×1024³)

The parameter argument can be given with or without space (`-b12m` is equivalent
to `-b 12m`).

`delay` is the desired delay, in milliseconds:
 * `12s` means 12 seconds (12×1000)
 * `12m` means 12 minutes (12×60×1000)
 * `12h` means 12 hours (12×60×60×1000)

The maximum expected bitrate provided by `delay` is the buffersize divided by
the delay.  For instance, `delay -b 10m 5s` will provide a maximum bitrate of
2Mb/s.

### Example

To delay the output of `command_A` to `command_B` by 5 seconds:

    command_A | delay 5s | command_B

If we need a 10Mb buffer:

    command_A | delay -b10m 5s | command_B

As an illustration, the following command produces an input at several rates,
and prints the result, delayed by 1 second, tabulated:

    { for i in {1..15}; do sleep $(bc <<< "scale=1;$i/10"); echo $i; done } |
        tee /dev/stderr | sed -u 's/^/\t/' | delay 1s

The following command allows a live-delayed video stream: it captures the webcam
and encodes the result in mpeg2, which is played in `vlc` with a delay of 2
seconds:

    ffmpeg -an -s 320x240 -f video4linux2 -i /dev/video0 -f mpeg2video -b 1M - |
        delay 2s | vlc -

## Blog post

<http://blog.rom1v.com/2014/01/lecture-differee-de-la-webcam-dun-raspberry-pi/>
(in French)
