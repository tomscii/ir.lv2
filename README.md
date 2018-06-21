IR: LV2 Convolution Reverb
==========================

![Screenshot](/sshot.png)

Author: Tom Szilagyi (tomszilagyi gmail com)  
License: GNU GPL v2  
Format: LV2 audio processing plugin  
Homepage: <http://tomszilagyi.github.io/plugins/ir.lv2>


IR is a no-latency/low-latency, realtime, high performance signal
convolver especially for creating reverb effects. Supports impulse
responses with 1, 2 or 4 channels, in any soundfile format supported
by libsndfile.

Developed by the author for their own needs, this plugin is released
to the public in the hope that it will be useful to others, but
strictly on an AS IS basis. There is ABSOLUTELY NO WARRANTY and there
is NO SUPPORT. High quality bug reports and well-tested patches are
nevertheless welcome. Other correspondence may be mercilessly ignored;
thank you for your understanding.


Features
--------

* Realtime convolution of impulse responses with stereo audio
* Supports Mono, Stereo and 'True Stereo' (4-channel) impulses
* Very reasonable CPU consumption
* Maximum impulse length: 1M samples (~22 seconds @ 48kHz)
* Loads a large number of audio file formats
* High quality sample rate conversion of impulse responses
* Stretch control (via high quality SRC in one step integrated with impulse loading)
* Pre-delay control (0-2000 ms)
* Stereo width control of input signal & impulse response (0-150%)
* Envelope alteration with immediate visual feedback:
  Attack time/percent, Envelope, Length
* Reverse impulse response
* Autogain: change impulses without having to adjust 'Wet gain'
* Impulse response visualization (linear/logarithmic scale, peak & RMS)
* Easy interface for fast browsing and loading impulse responses
* Free software released under the GNU GPL v2

Please note that resampling long impulses (several hundreds of
thousands of samples) on impulse loading may take several seconds. For
this reason it may be a good idea to convert one's impulse library to
all commonly used sample rates in advance, and use the plugin with the
appropriate impulse library on each session. (Hint: use Bookmarks!)


This is an LV2 plugin with its own GUI (GtkUI). You need an appropriate LV2
host that supports the following extensions:

* uiext: <http://lv2plug.in/ns/extensions/ui/>
* instance-access: <http://lv2plug.in/ns/ext/instance-access/>

The only seriously tested host at this time is Ardour 2.8.x (x >= 11);
Ardour 3 should also work (moderately tested). Other LV2 hosts should
also work, but YMMV. Compiled and tested on Linux only. Make sure to
read the **Known issues** section below.

Please note that due to close coupling between the plugin and its
GUI, bare-bones use of the plugin (without GUI) is not feasible.


Available versions
------------------

Two versions are available for download. **Version 1.2.x** (contained
in git branch `zero-latency`) is absolutely zero-latency, however
its run() should only be called with the same power-of-two buffer
sizes (eg. always 512 samples, or always 2048, etc) during the
plugin's lifecycle, otherwise bad things happen. In practice this
means that you should NEVER EVER use this version with its parameters
automated (even those listed as suitable for automation). Otherwise it
works as you would expect.

**Version 1.3.x** (in git branch `automatable`) uses additional
internal buffering so automation and the seemingly random buffer sizes
that occur when automating the plugin (calling run() with buffer
lengths eg. 1785, 263, 761, 1287 etc.) is not an issue anymore. It
works with automation (*but see the note about parameters suitable for
automation below*), at the expense of introducing extra buffering. The
resulting latency is reported to the host via standard LV2
mechanism. However, if this causes problems for you, feel free to use
version 1.2.x (and keep in mind that BOTH versions are completely
unsupported by the author).

Furthermore, there is a split of versions based on zita-convolver library
compatibility. The last versions to compile against and work with
zita-convolver 2 are 1.2.1 and 1.3.1. Later versions in both lines
(starting with 1.2.2 and 1.3.2) are compatible with zita-convolver 3.x.x
only. Versions starting with 1.2.4 and 1.3.4 are compatible with
zita-convolver 3.x.x as well as 4.x.x.

Note that versions in the `automatable` branch are not superior to the
corresponding `zero-latency` version; they are just different. HINT:
If you never use plugin automation on IR, try `zero-latency`. If you
absolutely need automation, or you have a problem with `zero-latency`
then use `automatable` and read the section about Automation below.

Known issues
------------

* When a plugin instance is created bypassed in Ardour (for example
  when copy&pasting from another IR instance), you have to un-bypass
  the new instance to be able to configure it. This is because the
  plugin needs to be run for at least one audio block in order to get
  properly initialised (...and this is because the LV2 run() function
  is the first place where a plugin can be certain about its input
  control values).

* Ardour2 version needs to be recent enough (2.8.12 or later), otherwise
  you will likely run into this problem with plugin automation:
  <http://tracker.ardour.org/view.php?id=3214>
  (This applies to IR 1.3. You don't want to automate IR 1.2 at all.)

* Currently Ardour does not have latency compensation on busses. If
  you use IR 1.3, and use it on eg. your drum bus, you may not get
  exactly what you want.

* When using a version with zita-convolver 2, you may or may not run
  into issues while freewheeling (eg. Ardour session export). In
  effect, zita-convolver does not wait for its worker threads to
  complete, so if the library is called too often (not in realtime),
  there may be "soft xruns" in the plugin's output. Please listen to
  the output carefully, or bounce in realtime. *There is no such
  problem with zita-convolver 3.x.x, so when in doubt, use the newer
  version!*

If you would like to build the plugin for debugging, please see the
Makefile for instructions.


Automation
----------

**This only applies to IR 1.3.x (automatable). Versions in the 1.2.x
  branch (zero-latency) are not suited to automation at all!**

The following plugin controls are internally smoothed (their effects are
protected from zipper noise) and thus are suitable for host automation:

 * Stereo width of input (but not of impulse response!)
 * Dry gain fader & on/off switch
 * Wet gain fader & on/off switch
 * Autogain on/off switch

Changing other controls result in an IR vector recalculation and
reinitialisation of the convolution engine. Those controls are **not**
suited for automation. Changes to non-automatable controls are
blissfully ignored when coming from automation (as opposed to real
user interaction).


Getting impulse responses
-------------------------

Any good IR files should work, but if you don't yet have any
favorites, try this:

* <http://www.echochamber.ch/index.php/impulseresponses>

There used to be a very good collection here, but seem like its gone:

* <http://rhythminmind.net/1313/?cat=182>
  (in particular, 'True M7' is a good selection of True Stereo impulses)

Note that besides mono and stereo impulse response files, IR supports
so-called 'True Stereo' impulses. These are four channel impulses that
describe a full convolution matrix (the four channels contain, in
order, the convolution paths L->L, L->R, R->L, R->R).

You can sometimes find a pair of stereo impulse response files with
similar names that end with L.wav and R.wav. In this case you have a
good chance of having found a 'True Stereo' response. To load it into
IR, you need to convert it to a single 4-channel file. You can do this
via sndfile-deinterleave and sndfile-interleave, but most probably you
will want to recursively convert a full directory tree of such files.

This is easiest done using the 'convert4chan' utility supplied with
this plugin. It recursively converts a directory tree containing
\*[LR].wav files to 4-channel files (\*4.wav). Build the utility by
issuing `make convert4chan`. The program takes zero or one
argument. In the former case, the directory tree under the current
working directory is converted, in the latter case the argument is
used as root directory. The conversion does not remove the source
files.

Note: a third file with a name that ends in C is often supplied along
the L and R files. Most probably this is a regular stereo version of
the same reverb. You will want to keep this besides the converted
(4-channel) True Stereo version.


Preset handling
---------------

Along with the usual floating point control data, the IR plugin needs
to store the impulse response filename as part of its configuration.
Unfortunately, there is no usable preset save/restore mechanism
standardized (yet) within the scope of the LV2 plugin format
standard. Because the LV2 host only saves and restores plugin control
port data (32-bit floating point numbers) as a means to session
persistence, it is necessary to implement a custom session-handling
mechanism to ensure that plugin instances get correctly saved and
automatically reloaded (even if the plugin UI is not readily opened
after a session reload).

The problem is solved by saving a 64-bit hash of the filename across
three floating point control ports. The corresponding hash-table
(along with global IR configuration data, eg. bookmarks) is stored in
the user's home directory in a file called `~/.ir_save` in human
readable form (however, modify it only if you know what you are doing,
and make sure to back it up first).

The important thing to remember is that this file contains information
needed to restore global IR settings, and IR plugin settings for any
and all plugin instances. When transferring a session to another
machine or making a backup, don't forget to copy or backup this file
as well.


Compile-time dependencies
-------------------------

The plugin uses the zita-convolver library as a backend for efficient
zero-latency realtime convolution computation. The libsamplerate
library is used for fast, high quality resampling of impulse responses
to the operational sample rate, taking into account the 'Stretch'
parameter (only one resampling is done). The well-known libsndfile
library is used for reading the impulse response sound files.

zita-convolver is written by Fons Adriaensen and can be downloaded
from: <http://kokkinizita.linuxaudio.org/linuxaudio/downloads/index.html>
Please note that zita-convolver-2.0.0 is required for IR versions up to
1.2.1 and 1.3.1; IR 1.2.2 / 1.3.2 and later versions require the newer
zita-convolver-3.x.x library (these are incompatible library versions).
Versions starting with 1.2.4 and 1.3.4 are compatible with zita-convolver
3.x.x as well as 4.x.x.

The libsndfile and libsamplerate libraries are written by Erik de
Castro Lopo and are widely used throughout the Linux Audio
community. They should be available on any recent Linux
distribution. Recent versions of these libraries should work fine.

The plugin GUI uses the GTK toolkit. Version 2.16 or later is
required.


Compiling and installing
------------------------

If you have the above dependencies installed, a `make` and (as root)
`make install` is all you need to use the plugin. The LV2 plugin
bundle is installed under /usr/lib/lv2/ by default (please edit the
Makefile to suit your needs if this is not what you want).

Issue `make convert4chan` to build the convert4chan utility described
above. This is for local use only, it is not installed by `make
install` (and is not built by a simple `make`).

In case of doubt, please consult the Makefile. You should be able to
figure it out from there.

If you run into stability problems with running the plugin, please
consult the top of ir.cc and see if changing the parameters helps you.
Look for these definitions (after a block of explanatory comments):

    #define CONVPROC_SCHEDULER_PRIORITY 0
    #define CONVPROC_SCHEDULER_CLASS SCHED_FIFO
    #define THREAD_SYNC_MODE true

After changing these, simple recompile and reinstall the plugin.
