# Server browser network latency

The multiplayer browser measures each numeric server address in its existing background worker.
The list appears before measurements complete; stale refresh results are discarded and selection
is preserved when ping sorting changes row order. The Ping (ms) column supports sorting, keeps
unavailable values last and marks ICMP / TCP readings. Latency is never accepted from master JSON.

Probes prefer ICMP echo and fall back to a TCP handshake on the same address's port 443. The
fallback sends no application data. This is host network round-trip time, not master HTTP time
or game simulation latency. Addresses shared by multiple server ports share one measurement.
If neither method responds, the browser displays an unavailable value.

The queue is bounded to 256 addresses, eight workers and three seconds; individual methods have
500 ms timeouts. Windows uses native ICMP APIs; Linux datagram echo sockets work without elevated
permissions when permitted by the OS. There is no shell fallback, game connection or player entry.
The launcher mirrors this helper. No server protocol change or server restart is required.

Validation includes the 15 MasterServerProtocol tests, actual Windows VPS measurements, and
unprivileged IPv4/IPv6 Linux loopback probes. The implementation adds the Windows IP Helper
library and includes the matching MyGUI column/layout in the client package.
