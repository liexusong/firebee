# Firebee
Very simple unique ID generator, using HTTP protocol!

<b>Rely on libraries: `Libevent` and `Hiredis`</b>

<pre>
Starup options:
-r host:port  # set redis host and port
-l ip         # bind IP
-p port       # listen port
-d            # run as daemonize mode
</pre>

Example:
--------
http://your-host/gen

Distributed architecture
------------------------
<pre>
                  [Client]
                 /        \
        [Haproxy]        [Haproxy]
        /     \           /      \
[Firebee]  [Firebee]  [Firebee]  [Firebee]  ------> [Redis]
</pre>
