import socket
import sys

# Create a TCP/IP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
# Bind the socket to the port
#server_address = ('192.168.0.25', 10000)
server_address = (sys.argv[1], int(sys.argv[2]))
print('starting up on %s port %s' % server_address)
sock.bind(server_address)
sock.listen(1)

while True:
    # Wait for a connection
    print('waiting for a connection')
    connection, client_address = sock.accept()
    try:
        print('connection from', client_address)
        # Receive the data in small chunks and retransmit it
        while True:
            data = connection.recv(1024)
            print('received "%s"' % data)
            if data:
                print('sending data back to the client')
                connection.sendall(data)
                #connection.shutdown(socket.SHUT_WR)
                #break
            else:
                print('no more data from', client_address)
                break
            
    finally:
        # Clean up the connection
        connection.close()