Graphene Desktop
==========

The material-design themed desktop environment of VeltOS, an Arch Linux-based
operating system built entirely upon community input. Check out [velt.io](https://www.velt.io)
for more info.

Screenshot
--------

![Screenshot of VeltOS](/screenshot.png?raw=true)

Installing for Arch
--------

If you are running Arch Linux, you can install Graphene Desktop by adding
*cdn.velt.io/vosrepo/$arch* to your list of repositories and installing the
package *graphene-desktop* via pacman. [Click here for more detail.](https://velt.io/veltos-installation/)

Building Manually
--------

graphene-desktop uses the following libraries and packages:

    - cmake (build only)
    - accountsservice
    - gnome-control-center (optional)
    - gnome-menus
    - libpulse
    - libcmk (https://github.com/VeltTechnologies/cmk)
    - librsvg
    - mutter >= 3.22
    - nautilus (optional)
    - networkmanager (optional)
    - paper-gtk-theme-git >= 297 (optional)
    - paper-icon-theme-git >= 552 (optional)
	- polkit
	- systemd
	- upower (optional)

All of these are available from Arch's official repositories, the AUR,
or the vos repository. For any other Linux distros, you're on your own.

Download/clone this repo and

```bash

    cd graphene-desktop
    cmake .
    sudo make all install
```

To run, start Graphene from a login manager such as LightDM, or execute
'graphene-desktop' from an X terminal. Graphene can also run on Wayland
by executing 'graphene-desktop --wayland' from a non-X terminal.

Uninstall by running 'sudo make uninstall' in the graphene-desktop directory.
This only works when install\_manifest.txt, a file created after installing, is
available.

License
--------

As a whole, Graphene Desktop is licensed under the GPLv3. However, the majority
of source files are under the Apache License 2.0. *The license information for
each file is listed at the top of the file.*

Authors
--------

Aidan Shafran <zelbrium@gmail.com>
