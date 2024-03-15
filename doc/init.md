Sample init scripts and service configuration for palladiumd
==========================================================

Sample scripts and configuration files for systemd, Upstart and OpenRC
can be found in the contrib/init folder.

    contrib/init/palladiumd.service:    systemd service unit configuration
    contrib/init/palladiumd.openrc:     OpenRC compatible SysV style init script
    contrib/init/palladiumd.openrcconf: OpenRC conf.d file
    contrib/init/palladiumd.conf:       Upstart service configuration file
    contrib/init/palladiumd.init:       CentOS compatible SysV style init script

Service User
---------------------------------

All three Linux startup configurations assume the existence of a "palladium" user
and group.  They must be created before attempting to use these scripts.
The macOS configuration assumes palladiumd will be set up for the current user.

Configuration
---------------------------------

At a bare minimum, palladiumd requires that the rpcpassword setting be set
when running as a daemon.  If the configuration file does not exist or this
setting is not set, palladiumd will shut down promptly after startup.

This password does not have to be remembered or typed as it is mostly used
as a fixed token that palladiumd and client programs read from the configuration
file, however it is recommended that a strong and secure password be used
as this password is security critical to securing the wallet should the
wallet be enabled.

If palladiumd is run with the "-server" flag (set by default), and no rpcpassword is set,
it will use a special cookie file for authentication. The cookie is generated with random
content when the daemon starts, and deleted when it exits. Read access to this file
controls who can access it through RPC.

By default the cookie is stored in the data directory, but it's location can be overridden
with the option '-rpccookiefile'.

This allows for running palladiumd without having to do any manual configuration.

`conf`, `pid`, and `wallet` accept relative paths which are interpreted as
relative to the data directory. `wallet` *only* supports relative paths.

For an example configuration file that describes the configuration settings,
see `share/examples/palladium.conf`.

Paths
---------------------------------

### Linux

All three configurations assume several paths that might need to be adjusted.

Binary:              `/usr/bin/palladiumd`
Configuration file:  `/etc/palladium/palladium.conf`
Data directory:      `/var/lib/palladiumd`
PID file:            `/var/run/palladiumd/palladiumd.pid` (OpenRC and Upstart) or `/run/palladiumd/palladiumd.pid` (systemd)
Lock file:           `/var/lock/subsys/palladiumd` (CentOS)

The PID directory (if applicable) and data directory should both be owned by the
palladium user and group. It is advised for security reasons to make the
configuration file and data directory only readable by the palladium user and
group. Access to palladium-cli and other palladiumd rpc clients can then be
controlled by group membership.

NOTE: When using the systemd .service file, the creation of the aforementioned
directories and the setting of their permissions is automatically handled by
systemd. Directories are given a permission of 710, giving the palladium group
access to files under it _if_ the files themselves give permission to the
palladium group to do so (e.g. when `-sysperms` is specified). This does not allow
for the listing of files under the directory.

NOTE: It is not currently possible to override `datadir` in
`/etc/palladium/palladium.conf` with the current systemd, OpenRC, and Upstart init
files out-of-the-box. This is because the command line options specified in the
init files take precedence over the configurations in
`/etc/palladium/palladium.conf`. However, some init systems have their own
configuration mechanisms that would allow for overriding the command line
options specified in the init files (e.g. setting `PALLADIUMD_DATADIR` for
OpenRC).

### macOS

Binary:              `/usr/local/bin/palladiumd`
Configuration file:  `~/Library/Application Support/Palladium/palladium.conf`
Data directory:      `~/Library/Application Support/Palladium`
Lock file:           `~/Library/Application Support/Palladium/.lock`

Installing Service Configuration
-----------------------------------

### systemd

Installing this .service file consists of just copying it to
/usr/lib/systemd/system directory, followed by the command
`systemctl daemon-reload` in order to update running systemd configuration.

To test, run `systemctl start palladiumd` and to enable for system startup run
`systemctl enable palladiumd`

NOTE: When installing for systemd in Debian/Ubuntu the .service file needs to be copied to the /lib/systemd/system directory instead.

### OpenRC

Rename palladiumd.openrc to palladiumd and drop it in /etc/init.d.  Double
check ownership and permissions and make it executable.  Test it with
`/etc/init.d/palladiumd start` and configure it to run on startup with
`rc-update add palladiumd`

### Upstart (for Debian/Ubuntu based distributions)

Upstart is the default init system for Debian/Ubuntu versions older than 15.04. If you are using version 15.04 or newer and haven't manually configured upstart you should follow the systemd instructions instead.

Drop palladiumd.conf in /etc/init.  Test by running `service palladiumd start`
it will automatically start on reboot.

NOTE: This script is incompatible with CentOS 5 and Amazon Linux 2014 as they
use old versions of Upstart and do not supply the start-stop-daemon utility.

### CentOS

Copy palladiumd.init to /etc/init.d/palladiumd. Test by running `service palladiumd start`.

Using this script, you can adjust the path and flags to the palladiumd program by
setting the PALLADIUMD and FLAGS environment variables in the file
/etc/sysconfig/palladiumd. You can also use the DAEMONOPTS environment variable here.

### macOS

Copy org.palladium.palladiumd.plist into ~/Library/LaunchAgents. Load the launch agent by
running `launchctl load ~/Library/LaunchAgents/org.palladium.palladiumd.plist`.

This Launch Agent will cause palladiumd to start whenever the user logs in.

NOTE: This approach is intended for those wanting to run palladiumd as the current user.
You will need to modify org.palladium.palladiumd.plist if you intend to use it as a
Launch Daemon with a dedicated palladium user.

Auto-respawn
-----------------------------------

Auto respawning is currently only configured for Upstart and systemd.
Reasonable defaults have been chosen but YMMV.
