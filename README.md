Serval VoMP Channel Driver for Asterisk
=======================================
[Serval Project][], Sept 2014

This repository contains the source code for the [Serval Project][]'s [VoMP][]
channel driver for [Asterisk 11][] implemented in [GNU C][], together with an
[AGI][] script in [Python][] and a set of configuration file templates for
Asterisk.

The VoMP channel driver allows users of a [Serval mesh network][] to dial
numbers outside the local mesh and vice versa, for example, to the [PSTN][] via
a [VoIP][] provider, or to a telephone network with an Asterisk gateway, such
as the [Mesh Potato][] by [VillageTelco][] or [Commotion OpenBTS][] by the
[Commotion Wireless][] project.

See [README-OpenBTS](./README-OpenBTS.md) for more information about Commotion
OpenBTS integration.

The channel driver has been tested with Asterisk 11.7.0 and will probably
work with higher versions of 11.x.

The VoMP Asterisk channel driver is only available in source code form, not in
pre-built (binary) form.  The channel driver source code must be compiled into
a shared library (binary) which must be placed in the directory from which
Asterisk loads modules.  Asterisk must then be configured with a dial plan that
uses the VoMP channel driver module to route calls to and from the Serval mesh.
These steps are described below.

Concept of operation
--------------------

A VoMP gateway is a server or node connected to both the [Serval mesh
network][] and to an external network (such as the Internet), and running the
integrated [Asterisk 11][] and [Serval DNA][] daemons integrated using this
channel driver.  The gateway has its own [SID][] (Serval subscriber
identifier).

Calls are routed out of the Serval mesh by having the VoMP gateway respond to
[DNA][] (phone lookup) requests by invoking a [DNA Helper][] script to test
whether the [DID][] (phone number) is reachable via Asterisk.  If so, the
gateway responds to the DNA request with a VoMP URI containing its own [SID][].
The caller then places the call by sending a VoMP CALL request to the gateway's
SID, specifying the final (destination) phone number that was originally
queried.  The [Serval DNA][] daemon on the gateway then initiates the call
through Asterisk by sending commands via the channel driver, and bridges the
VoMP audio stream to and from Asterisk via the channel driver.

Calls are routed into the Serval mesh if an Asterisk dial plan invokes the
channel driver's [AGI][] script to resolve a [DID][].  The AGI script invokes
the [Serval DNA][] binary to perform a [DNA][] request on the Serval mesh
network.  All the reachable nodes in the Serval mesh which match the DID
(including other gateways) will reply to the request with a VoMP URI containing
theid [SID][].  If any DNA reply is received, the AGI script will return a
positive result to the Asterisk dial plan, which will choose one of the URIs.
If the chosen URI is a VoMP URI, then Asterisk will command the Serval DNA
daemon via the channel driver to initiate a call to the given SID, and the
channel driver will then bridge the VoMP audio stream between Asterisk and
the Serval DNA daemon.

The channel driver performs audio stream conversion between the VoMP codec and
Asterisk's internal audio stream format.  The channel driver also handles codec
negotiation and call tear-down.

Dependencies
------------

In order to compile the VoMP Asterisk channel driver from source code, a
current build of [Asterisk 11][] from source code is needed.  The channel
driver compilation reads build options (such as compiler flags) and header
files from the Asterisk build.  (If you know of any way to get these from an
Asterisk installation or development package without downloading and compiling
the entire Asterisk source code, please let us know!)

In order to compile the VoMP Asterisk channel driver from source code, a
current build of [Serval DNA][] from source code is needed.  The channel driver
compilation reads header files and the monitor client library from the Serval
DNA build.  (It is planned to eventually revise the monitor client interface
and provide a Serval development kit that does not require the whole source
code of Serval DNA, but until then, the Serval DNA source code must be
downloaded and built.)

### Build Asterisk 1.8

Asterisk 1.8 must be built from source code.  The following example downloads
and builds the latest stable 1.8 series:

    $ cd $HOME/src
    $ wget -q http://downloads.asterisk.org/pub/telephony/asterisk/asterisk-1.8-current.tar.gz
    $ tar xzf asterisk-1.8-current.tar.gz
    $ cd asterisk-1.8.20.0
    $ ./configure
    ...
    $ make
    ...

### Install Asterisk 1.8

This step is not necessary in order to compile the VoMP channel driver, but is
provided to illustrate the steps needed to build an Asterisk node from scratch.

The following example installs the Asterisk software under the default prefix
(`/usr`) and installs the basic Asterisk ("factory") configuration under
`/etc/asterisk` without modification.  This is unlikely to suit your needs, and
is for illustration only:

    $ sudo su
    # make install
    ...
    # make config
    ...
    #

You may have to edit the installed `/etc/defaults/asterisk` to set the
`AST_USER` and `AST_GROUP` variables appropriately.

### Build Serval DNA

[Serval DNA][] source code is available from the [serval-dna][] repository on
GitHub:  You can use [Git][] to download the latest version:

    $ cd $HOME/src
    $ git clone -q git://github.com/servalproject/serval-dna.git
    $ cd serval-dna
    $

Follow the build instructions in [Serval DNA INSTALL.md][].

### Install Serval DNA

This step is not necessary in order to compile the VoMP channel driver, but
will simplify the configuration of Asterisk by making Serval DNA executables
available at a known path.  For example, on Linux:

    $ sudo su
    # cp -p servald directory_service /usr/sbin
    #

Configure Serval DNA
--------------------

Follow the configuration instructions in [Servald-Configuration.md][].

The [Serval DNA][] daemon must be configured with the UID of the running
Asterisk daemon process, to allow the channel driver to communicate with the
[Serval DNA][] daemon.  This must match the `AST_USER` variable in the
`/etc/defaults/asterisk` file and must be numeric, not a login name.

For example, add the following line to `/var/serval-node/serval.conf`:

    monitor.uid=106

Without this line, or with an incorrect UID, [Serval DNA][] will reject
connections from the channel driver.

Download channel driver
-----------------------

The source code for the VoMP channel driver is is available from the
[app\_servaldna][] repository on GitHub.  You can use [Git][] to download the
latest version:

    $ cd $HOME/src
    $ git clone -q git://github.com/servalproject/app_servaldna.git
    $ cd app_servaldna
    $

Build channel driver
--------------------

The `AST_ROOT` and `SERVAL_ROOT` variables defined at the top of the
[Makefile](./Makefile) must be set to the directories containing the built
Asterisk source code and the built [serval-dna][] source code respectively (see
above).  This can be done in two ways:

1. EITHER edit the two lines in Makefile then run the `make` command,

2. OR pass arguments to the `make` command (which will override the settings
   in `Makefile`), eg:

        $ make AST_ROOT=$HOME/src/asterisk-1.8.20.0 SERVAL_ROOT=$HOME/src/serval-dna
        $

The channel driver build process creates **app_servaldna.so**, which is the
VoMP channel driver module shared library for [Asterisk 1.8][].  In addition to
this, **servaldnaagi.py** is an AGI script invoked by Asterisk which resolves a
[DID][] (phone number) to a [SID][] (Serval subscriber identifier) or SIP URI.

Install channel driver
----------------------

Copy the shared library and AGI script into the proper Asterisk configuration
directories.  For example, on Linux:

    $ cp -p app_servaldna.so /usr/lib/asterisk/modules
    $ cp -p servaldnaagi.py /usr/lib/asterisk
    $

For example, on Mac OS X:

    $ cp -p app_servaldna.so "/Library/Application Support/Asterisk/Modules/modules"
    $ cp -p servaldnaagi.py "/Library/Application Support/Asterisk/"
    $

Configure Asterisk to use the channel driver
--------------------------------------------

Two alternative sample configurations are provided, basic and advanced:

* the [conf](./conf/) directory contains a set of basic sample Asterisk configuration
  files.  See below for instructions.

* the [conf\_adv](./conf_adv/) directory contains an advanced set of sample
  Asterisk configuration files that were developed for [Commotion OpenBTS][]
  integration testing.  It does not provision numbers statically in
  `extensions.conf`, but instead looks up the OpenBTS registered subscriber
  database.  See [README-OpenBTS](./README-OpenBTS.md) for instructions.

### Basic sample configuration

The basic sample configuration provisions all extensions statically in
`extensions.conf`.  This allows the channel driver to resolve numbers directly
by asking Asterisk what is in the dialplan if `resolve_numbers` is true (false
by default).  This will result in servald responding to incoming DNA requests
with its own [SID][] for all numbers provisioned in `extensions.conf`.

For example, on Linux, copy the Asterisk configuration files:

    $ sudo su
    # cp conf/* /etc/asterisk
    #

then update the `extensions.conf` and `servaldna.conf` files as described
below.

### Update extensions.conf

Edit the copied `/etc/asterisk/extensions.conf` to set the paths of the AGI
script, the [Serval DNA][] executable, and the Serval DNA instance directory.
For example, to use the paths from previous examples and Serval DNA's default
built-in instance path:

    SERVAL_AGI = /usr/lib/asterisk/servaldnaagi.py
    SERVAL_BIN = /usr/sbin/servald
    SERVALD_INSTANCE = /var/serval-node

### Update servaldna.conf

Edit the copied `/etc/asterisk/servaldna.conf` to set the `instancepath`
variable to the absolute path of the instance directory used by the [Serval
DNA][] daemon.  This will allow the channel driver to communicate with the
daemon.  For example, to use Serval DNA's default built-in instance path:

    instancepath = /var/serval-node

About the examples
------------------

The examples in this document are [Bourne shell][] commands, using standard
quoting and variable expansion.  Commands issued by the user are prefixed with
the shell prompt `$` or `#` to distinguish them from the output of the command.
A prompt of `#` indicates that the command must be executed as the super user.
Single and double quotes around arguments are part of the shell syntax, so are
not seen by the command.  Lines ending in backslash `\` continue the command on
the next line.

The directory paths used in the examples are for illustrative purposes only,
and are not the recommended values for a production system.

Copyright and licensing
-----------------------

The VoMP Asterisk channel driver is [free software][] initially produced by the
[Serval Project][].  It is licensed to the public under the [GNU General Public
License version 2][GPL2].  All source code is freely available from the Serval
Project's [app\_servaldna][] Git repository on [GitHub][].

The copyright in the source code is held by [Serval Project Inc.][SPI], a
not-for-profit association incorporated in the state of South Australia in the
Commonwealth of Australia for the purpose of developing the Serval mesh
software.

The [Serval Project][] will accept contributions from individual developers who
have agreed to the [Serval Project Developer Agreement - Individual][individ],
and from organisations that have agreed to the [Serval Project Developer
Agreement - Entity][entity].

More information
----------------

For further information about this software, including bug reports, development
policies and practices, etc., please see the [Serval Project Wiki][].


[Serval Project]: http://www.servalproject.org/
[Serval Project Wiki]: http://developer.servalproject.org/dokuwiki/doku.php
[Serval mesh network]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:mesh_network
[VoMP]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:vomp
[Asterisk 1.8]: http://www.asterisk.org/downloads/asterisk-news/asterisk-180-released
[Serval DNA]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:servaldna:
[DNA]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:dna
[DNA Helper]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:dna_helper
[DID]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:did
[SID]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:sid
[PSTN]: http://en.wikipedia.org/wiki/Public_switched_telephone_network
[VoIP]: http://en.wikipedia.org/wiki/Voice_over_IP
[Mesh Potato]: http://villagetelco.org/mesh-potato/
[VillageTelco]: http://villagetelco.org/about/
[Commotion OpenBTS]: https://commotionwireless.net/projects/openbts
[Commotion Wireless]: https://commotionwireless.net/
[serval-dna]: https://github.com/servalproject/serval-dna
[Serval DNA INSTALL.md]: https://github.com/servalproject/serval-dna/blob/development/INSTALL.md
[Servald-Configuration.md]: https://github.com/servalproject/serval-dna/blob/development/doc/Servald-Configuration.md
[app\_servaldna]: https://github.com/servalproject/app_servaldna
[SPI]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:spi
[AGI]: http://en.wikipedia.org/wiki/Asterisk_Gateway_Interface
[GNU C]: http://gcc.gnu.org/
[Python]: http://python.org/
[free software]: http://www.gnu.org/philosophy/free-sw.html
[Git]: http://git-scm.com/
[GitHub]: https://github.com/servalproject
[GPL2]: http://www.gnu.org/licenses/gpl-2.0.html
[Bourne shell]: http://en.wikipedia.org/wiki/Bourne_shell
[individ]: http://developer.servalproject.org/files/serval_project_inc-individual.pdf
[entity]: http://developer.servalproject.org/files/serval_project_inc-entity.pdf
