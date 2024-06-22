# spicy-kvm

> [!NOTE]
> This tool is still a work-in-progress, and I mostly made this for my own specific use case. The configuration is currently hard-coded, logging is racy, error messages may be obscure, and the code is somewhat messy. Some nice-to-have features are also missing. Everything more or less works (I've been using this since early 2024), but for now, you'll need to modify the code. I am not accepting pull requests.

Almost like a KVM switch, but using SPICE, a virtual machine, and a dedicated GPU with a regular monitor connected to both machines.

A tool to connect to GPU-passthrough virtual machines on the local network with a hotkey, forwarding input/audio over SPICE, and switching monitor outputs using DDC-CI.

It is designed for a very specific use case:

- You require low-latency audio/video/input for a virtual machine
  - Other remote desktop solutions like RealVNC or xfreerdp are simpler if you don't.
- The virtual machine is on the same host or the local network.
  - SPICE is TCP-based, and all input and audio is sent through it, so a bad network connection will result in severe performance issues.
  - Audio is low-latency if the network is, but it is not directly synchronized with the video output, so if both the video and audio aren't low-latency, they may end up out of sync.
- You are using GPU passthrough, and the machine is connected to a monitor (possibly the same one you're using for another machine -- you can use DDC-CI to automatically switch outputs).
- You want to pass raw input from a physical evdev input device to the virtual machine without physically disconnecting it and/or want to be able to switch using a hotkey.
- You want to use audio input/output from your client machine in the virtual machine simultaneously.
- You want to be able to still use your client machine remotely or on other screens with other input devices while accessing the virtual machine.

You can think about it like:

- Looking-glass, but for a VM on the local network.
- A poor-man's KVM switch and audio mixer.
- Moonlight/GameStream/virt-viewer, but with a real screen and lower latency.

If any of the above isn't true, you're better off:

- Using SPICE directly with virt-viewer for local or local network VMs.
- Using LookingGlass for local VMs with a dedicated GPU.
- Using Moonlight/GameStream for gaming on a remote computer (the input and audio latency will be higher, but it's still generally usable).
- Using RDP or VNC for general-purpose remote desktop access (RealVNC is very nice for remote desktop on Linux).

The audio buffering and device code is based on looking-glass.

```
dnf install cmake gcc g++ kernel-headers 'pkgconfig('{spice-protocol,samplerate,libpipewire-0.3,nettle,hogweed}')'
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -GNinja
ninja -C build
```

Ensure SPICE is enable and exposed over TCP. VirtIO input devices are also required.

<!--
```
usage: spicy-kvm [options]

 --spice-host                   spice host
 --spice-port                   spice port
 --spice-password PASSWORD      spice password
 --spice-password-env           read PASSWORD as en environment variable name
 --spice-password-file          read PASSWORD as a file

 --activate                     automatically activate upon startup
 --oneshot                      exit after the first deactivation

 --on KEYCODE                   activate when the specified key is released (requires an input device to be specified)
 --off KEYCODE                  deactivate when the specified key is pressed (requires an input device to be specified)

 --record DEVICE                enable audio input from the specified pipewire device (empty for the default)
 --record-persistent            keep audio recording connected even when not active

 --playback DEVICE              enable audio output to the specified pipewire device (empty for the default)
 --playback-persistent          keep audio playback connected even when not active

 --input DEVICE                 enable keyboard/mouse input from the specified evdev device name, path, or number (can be specified multiple times)
 --input-auto                   watch all accessible evdev devices and enable keyboard/mouse input from the evdev device which pressed the activation key and the next pointing device to send an EV_REL event

 --ddc DEVICE                   use the i2c bus associated with the specified drm card name (including the "card" prefix) or i2c bus number for ddc-ci
 --ddc-vcp VCP,INACTIVE,ACTIVE  set the specified ddc-ci vcp when activating/deactivating (all values are hex) (can be repeated multiple times) (use `ddcutil capabilities` to figure out what can go here) (you can use this for automatically switching monitor inputs)
```
-->
