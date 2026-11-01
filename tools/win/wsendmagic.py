# Windows Python: send datagrams to a running game from the peer's own UDP endpoint (127.0.0.1:<peer port>), as that peer would. A game accepts a datagram only from the endpoint of the member it names.
# usage: wsendmagic.py <game port> <peer port> <sender id> <magic> [count]     the datagram: magic (4 chars), sender id (u32), zero padding to 40 bytes (a wrong checksum: the sync core drops it)
import socket, struct, sys, time
gport, pport, sid, magic = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4].encode()
count = int(sys.argv[5]) if len(sys.argv) > 5 else 1
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('127.0.0.1', pport))
for _ in range(count):
    s.sendto(magic + struct.pack('<I', sid) + bytes(32), ('127.0.0.1', gport)); time.sleep(0.05)
print('sent', count, magic.decode(), 'from', pport, 'to', gport)
