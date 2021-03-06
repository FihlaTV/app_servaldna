Sample configuration to integrate Serval DNA with Commotion OpenBTS
===================================================================
[Serval Project][], May 2013

This directory is part of the [app\_servaldna][] component, the [VoMP][]
channel driver for [Asterisk 1.8][] developed by the [Serval Project][] to
[integrate][] the [Serval mesh network][] with [Commotion OpenBTS][].

The [main OpenBTS README](../README-OpenBTS.md) describes the concept of
operation and gives more background information.

The sample configuration in this directory causes Asterisk to query the
[OpenBTS][] registered subscribers database to resolve calls to its associated
[GSM][] phones, and to query the [Serval mesh network][] using the [DNA][]
protocol to resolve calls to any other phone.

Configuration files
-------------------

* [num2sip.py][] is a [DNA Helper][] script that allows the Serval DNA daemon
  to resolve [DID][] lookups using the OpenBTS subscriber registry database, so
  that [Serval Mesh][] users and other OpenBTS units can reach GSM phones.  It
  outputs a [URI][] containing the local [SID][] if the given phone number is
  found in the registry.

* [num2sip.ini][] is the configuration for `num2sip.py`, which
  contains the absolute path of the OpenBTS subscriber registry database, which
  must coincide with the same path configured in Asterisk.

* [serval.conf][] contains a minimal configuration for [Serval DNA][] which
  causes it to use [num2sip.py][] as a [DNA Helper][] script.  It will most
  likely need to be merged into a larger configuration file, as shown in an
  example below.  See [Servald-Configuration.md][] for more information.

* the [asterisk](./asterisk) directory contains the following configuration
  files for [Asterisk 1.8][]:

    * [logger.conf](./asterisk/logger.conf) increases the logging verbosity to
      make debugging simpler, and is optional.

    * [cdr.conf](./asterisk/cdr.conf) and
      [indications.conf](./asterisk/indications.conf) are boiler plate files
      copied unmodified from the Asterisk example configuration.

    * [modules.conf](./asterisk/modules.conf) is a minimal file to make sure
      auto module loading is on.  If you are debugging by running `asterisk -r`
      then running `core set verbose 3` will cause a lot of things to be
      printed when a call is made.

    * [func\_odbc.conf](./asterisk/func_odbc.conf) tells the ODBC module to
      register a function which lets the dial plan perform any SQL query on the
      OpenBTS database.

    * [sip.conf](./asterisk/sip.conf) causes Asterisk to bind to the default
      port (5060) and treat incoming connections from 127.0.0.1:5062 for the
      "openbts" context (used in the dial plan).

    * [servaldna.conf](./asterisk/servaldna.conf) gives the channel driver the
      instance path of the [Serval DNA][] daemon so that it can access the
      monitor socket.  It also sets the context name (`incoming-trunk` in this
      case) for incoming connections so they can be sorted out in the dial
      plan. If `resolve_numbers` is true then the channel driver will resolve
      numbers for the Serval daemon by looking for matching patterns in the
      dial plan, but since we are using dynamic lookups in the database we
      cannot use this feature.

    * [extensions.conf](./asterisk/extensions.conf) is the Asterisk dial plan –
      this controls how calls are routed and can do virtually anything.  It
      contains several sections:

        + The `[globals]` section defines several variables which are explained
          in [“Update extensions.conf” section in README.md]
          (../README.md#update-extensionsconf).

        + The `[test]` context provides 2 numbers – 2600 and 2601 – which can
          be used to test handsets.

        + The `[incoming-trunk]` context handles incoming [VoMP][] calls, using
          the `call-local` macro to look up then call a GSM handset.

        + The `[openbts]` context handles calls from GSM handsets.  First it
          fixes their caller ID by translating the [IMSI][] to a phone number
          using the subscriber registry database.  Next, it checks if calling a
          test number or is a local call (using the `call-local` macro).  If
          neither, then it goes to `outbound-trunk` which uses the [AGI][]
          script to ask the [Serval DNA][] daemon to resolve the number.  If
          this succeeds, Asterisk will connect to the remote URI generated by
          the Serval daemon and bridge it with the [SIP][] call from the GSM
          handset.

Installation
------------

For example, on Linux, copy the Asterisk configuration files:

    $ sudo su
    # cp conf_adv/asterisk/* /etc/asterisk
    #

then update the installed `extensions.conf` and `servaldna.conf` as described
in [README “Update extensions.conf”](../README.md#update-extensionsconf) and
[README “Update servaldna.conf”](../README.md#update-servaldnaconf).

For example, on Linux, copy the DNA Helper script and its configuration file:

    $ sudo su
    # cp conf_adv/num2sip.py /usr/lib/asterisk
    # cp conf_adv/num2sip.ini /etc/asterisk
    #

For example, add the following lines to `/var/serval-node/serval.conf`:

    dna.helper.executable=/usr/lib/asterisk/num2sip.py
    dna.helper.argv.1=/etc/asterisk/num2sip.py

Other configuration
-------------------

The ODBC module must be configured to allow Asterisk to query the SQLite
subscriber registry database directly from within a dial plan.  For testing,
the following lines were added to `/etc/odbc.ini`:

    [asterisk]
    Description=SQLite3 database
    Driver=SQLite3
    Database=/var/lib/asterisk/sqlite3dir/sqlite3.db
    # optional lock timeout in milliseconds
    Timeout=2000

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
and are not the recommended values for a production system.  They coincide with
the paths used in [README.md](../README.md).


[Serval Project]: http://www.servalproject.org/
[app\_servaldna]: ../README.md
[Commotion OpenBTS]: https://commotionwireless.net/projects/openbts
[Serval DNA]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:servaldna:
[Serval mesh network]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:mesh_network
[Serval Mesh]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:servalmesh:
[Servald-Configuration.md]: https://github.com/servalproject/serval-dna/blob/development/doc/Servald-Configuration.md
[integrate]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:commotion_openbts
[OpenBTS]: http://wush.net/trac/rangepublic/wiki
[Asterisk 1.8]: http://www.asterisk.org/downloads/asterisk-news/asterisk-180-released
[GSM]: http://en.wikipedia.org/wiki/GSM
[VoMP]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:vomp
[DNA]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:dna
[DNA Helper]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:dna_helper
[DID]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:did
[SID]: http://developer.servalproject.org/dokuwiki/doku.php?id=content:tech:sid
[IMSI]: http://en.wikipedia.org/wiki/International_mobile_subscriber_identity
[SIP]: http://en.wikipedia.org/wiki/Session_Initiation_Protocol
[AGI]: http://en.wikipedia.org/wiki/Asterisk_Gateway_Interface
[URI]: http://en.wikipedia.org/wiki/Uniform_resource_identifier
[JNI]: http://en.wikipedia.org/wiki/Java_Native_Interface
[OLSR]: http://www.olsr.org/
[num2sip.py]: ./num2sip.py
[num2sip.ini]: ./num2sip.ini
[serval.conf]: ./serval.conf
[Bourne shell]: http://en.wikipedia.org/wiki/Bourne_shell
