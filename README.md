------------------------------------------------------------------------
AGL-AudioUtils sample of AGL Audio Bindings usage
------------------------------------------------------------------------
References:
    http://www.linuxjournal.com/node/6735/print
    http://equalarea.com/paul/alsa-audio.html
    http://alsa.opensrc.org/How_to_use_softvol_to_control_the_master_volume

Test sound
  aplay -l # list devices
  aplay -L # list PCM
  speaker-test -c 2 -D $DEVID -twav  # number of channel should match with PCM

Testing Jabra USB Solemate
  SNDCARD=hw:v1340
  amixer -D $SNDCARD controls         # check numid volume controls
  amixer -D $SNDCARD cget numid=4     # check master PCM volume
  amixer -D $SNDCARD cset name="PCM Playback Volume" 50%
  amixer -D $SNDCARD cset numid=4 7   # equivalent with numid instead of label

Config Alsa to enable softmixer+softvolume
 cp audio-utils/template/asoundrc-usb ~/.asoundrc # (Alternative at /etc/asound.conf for system wide config)
 edit $/HOME/.asoundrc          # customise to reflect your config
 /usr/sbin/alsactl kill rescan  # valid new config file

Test new installed PCM
  speaker-test  -D plug:music -c 2 -twav
  speaker-test  -D plug:notif -c 2 -twav
  speaker-test  -D plug:navi  -c 2 -twav

Test Audio multiplexing
  aplay -D plug:music  audio-utils/htdocs/sounds/trio-divi-alkazabach.wav &
  speaker-test  -D plug:navi  -c 2 -twav

Check softvolume PCM
   amixer -D $SNDCARD controls  # WARNING: softcontrol are only visible after SoftPCM 1st usage
   amixer -D $SNDCARD cget name="MasterMusic" # Get current value of software for Music 
   amixer -D $SNDCARD cset name="MasterMusic" 80%
   amixer -D $SNDCARD cset name="MasterNavi"  100%
   amixer -D $SNDCARD cset numid=9 50% # equivalent with numid

Preparing UCM config (WARNING: soft volume are only visible after 1st usage (e.g. "speaker-test -Dplug:music -c2 -twav")
   amixer -D $SNDCARD cset name="MasterMusic" 80%
   amixer -D $SNDCARD cset name="MasterNavi"  100%
   aplay  -D plug:music  audio-utils/htdocs/sounds/trio-divi-alkazabach.wav &
   amixer -D $SNDCARD cset name="MasterMusic" 60%; speaker-test  -D navi  -c 2 -twav -l 2; amixer -D $SNDCARD cset name="MasterMusic" 80%


--------------------------------------------------------------------------------------------
  Compiling audio-utils
--------------------------------------------------------------------------------------------

Dependencies
  afb-daemon
  afb-audio

```
# Compile
INSTALL_DIR=xxxx # default ./Install
mkdir build; cd build
cmake -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR ..
make install

# Start From Development Tree
  afb-daemon --verbose --token="" --ldpaths=./build --port=1235 --roothttp=./htdoc

# Start From $INSTALL_DIR
  afb-daemon --verbose --token="" --ldpaths=$INSTALL_DIR/lib/utils --port=1235 --roothttp=$INSTALL_DIR/htdocs/utils-bindings
```
# replace hd:XX with your own sound card ID ex: "hw:0", "hw:PCH", ...
Start a browser on http://localhost:1235   # WARNING: should not conflic with audio-binding port
