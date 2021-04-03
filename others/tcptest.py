import socket
import sys
import time

# Create a TCP/IP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# Connect the socket to the port where the server is listening
server_address1 = ('192.168.0.25', 10000)
server_address2 = ('192.168.0.25', 10001)
print("Connect to:",server_address1)
sock.connect(server_address1)
time.sleep(2)
sock.shutdown(socket.SHUT_RDWR)
time.sleep(2)
print("Connect to:",server_address2)
sock.connect(server_address2)
