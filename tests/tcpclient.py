import socket
import sys

# Create a TCP/IP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# Connect the socket to the port where the server is listening
server_address = ('192.168.0.25', 10000)
print >>sys.stderr, 'connecting to %s port %s' % server_address
print(sock.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF))
sock.connect(server_address)
#sock.setblocking(0)
try:
    
    # Send data
    message = 1024*'1'
    print >>sys.stderr, 'sending "%s"' % message
    l = len(message)
    all = 0
    #while l == len(message):
    l = sock.send(message)
    all += l
        #print(all)

    print("all sent",all)

    # Look for the response
    amount_received = 0
    amount_expected = all
    #input("Press Enter to read...")
    while amount_received < amount_expected:
        data = sock.recv(1024)
        amount_received += len(data)
        print >>sys.stderr, 'received "%s"' % data

finally:
    print >>sys.stderr, 'closing socket'
    sock.close()
