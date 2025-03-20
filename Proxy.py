# Include the libraries for socket and system calls
import socket
import sys
import os
import argparse
import re
import time

def parse_http_response(response):
    try:
        response_parts = response.split(b'\r\n\r\n', 1)
        headers_bytes = response_parts[0]
        headers = headers_bytes.decode('utf-8')
        
        header_lines = headers.split('\r\n')
        status_line = header_lines[0]
        status_parts = status_line.split(' ', 2)
        status_code = status_parts[1]
        
        headers_dict = {}
        for line in header_lines[1:]:
            if ': ' in line:
                key, value = line.split(': ', 1)
                headers_dict[key.lower()] = value
        
        body = response_parts[1] if len(response_parts) > 1 else b''
        
        return {
            'status_code': status_code,
            'headers': headers_dict,
            'body': body,
            'raw_headers': headers_bytes,
            'raw_response': response
        }
    except Exception as e:
        print(f"Error parsing HTTP response: {e}")
        return {
            'status_code': '500',
            'headers': {},
            'body': b'',
            'raw_headers': b'',
            'raw_response': response
        }

# 1MB buffer size
BUFFER_SIZE = 1000000

# Get the IP address and Port number to use for this web proxy server
parser = argparse.ArgumentParser()
parser.add_argument('hostname', help='the IP Address Of Proxy Server')
parser.add_argument('port', help='the port number of the proxy server')
args = parser.parse_args()
proxyHost = args.hostname
proxyPort = int(args.port)

# Create a server socket, bind it to a port and start listening
try:
    # Create a server socket
    # ~~~~ INSERT CODE ~~~~
    serverSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # ~~~~ END CODE INSERT ~~~~
    print ('Created socket')
except:
    print ('Failed to create socket')
    sys.exit()

try:
    # Bind the the server socket to a host and port
    # ~~~~ INSERT CODE ~~~~
    serverSocket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    serverSocket.bind((proxyHost, proxyPort))
    # ~~~~ END CODE INSERT ~~~~
    print ('Port is bound')
except:
    print('Port is already in use')
    sys.exit()

try:
    # Listen on the server socket
    # ~~~~ INSERT CODE ~~~~
    serverSocket.listen(5)
    # ~~~~ END CODE INSERT ~~~~
    print ('Listening to socket')
except:
    print ('Failed to listen')
    sys.exit()

# continuously accept connections
while True:
    print ('Waiting for connection...')
    clientSocket = None

    # Accept connection from client and store in the clientSocket
    try:
      # ~~~~ INSERT CODE ~~~~
      clientSocket, clientAddress = serverSocket.accept()
      # ~~~~ END CODE INSERT ~~~~
      print ('Received a connection')
    except:
      print ('Failed to accept connection')
      sys.exit()

    # Get HTTP request from client
    # and store it in the variable: message_bytes
    # ~~~~ INSERT CODE ~~~~
    message_bytes = clientSocket.recv(BUFFER_SIZE)
    # ~~~~ END CODE INSERT ~~~~
    message = message_bytes.decode('utf-8')
    print ('Received request:')
    print ('< ' + message)

    # Extract the method, URI and version of the HTTP client request 
    requestParts = message.split()
    method = requestParts[0]
    URI = requestParts[1]
    version = requestParts[2]

    print ('Method:\t\t' + method)
    print ('URI:\t\t' + URI)
    print ('Version:\t' + version)
    print ('')

    # Get the requested resource from URI
    # Remove http protocol from the URI
    URI = re.sub('^(/?)http(s?)://', '', URI, count=1)

    # Remove parent directory changes - security
    URI = URI.replace('/..', '')

    # Split hostname from resource name
    resourceParts = URI.split('/', 1)
    hostname = resourceParts[0]
    resource = '/'

    if len(resourceParts) == 2:
      # Resource is absolute URI with hostname and resource
      resource = resource + resourceParts[1]

    print ('Requested Resource:\t' + resource)

    # Check if resource is in cache
    try:
      cacheLocation = './' + hostname + resource
      if cacheLocation.endswith('/'):
          cacheLocation = cacheLocation + 'default'

      print ('Cache location:\t\t' + cacheLocation)

      fileExists = os.path.isfile(cacheLocation)
          
      if fileExists:
          cache_time = os.path.getmtime(cacheLocation)
          current_time = time.time()
          
          with open(cacheLocation, "rb") as f:
              cache_content = f.read()
              
              if cache_content.startswith(b"X-Cache-Control: max-age="):
                  first_line_end = cache_content.find(b'\r\n')
                  if first_line_end != -1:
                      control_line = cache_content[:first_line_end].decode('utf-8', errors='ignore')
                      try:
                          max_age = int(control_line.split('=')[1].strip())
                          
                          if max_age > 0 and (current_time - cache_time) > max_age:
                              print(f"Cache expired! max-age={max_age}, age={(current_time - cache_time)}")
                              raise Exception("Cache expired")
                          
                          cache_content = cache_content[first_line_end + 2:]
                      except:
                          pass
              
              print ('Cache hit! Loading from cache file: ' + cacheLocation)
              clientSocket.sendall(cache_content)
              print ('Sent to the client from cache')
      else:
          raise Exception("Cache file not found")

    except:
      # cache miss.  Get resource from origin server
      originServerSocket = None
      # Create a socket to connect to origin server
      # and store in originServerSocket
      # ~~~~ INSERT CODE ~~~~
      originServerSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
      # ~~~~ END CODE INSERT ~~~~

      print ('Connecting to:\t\t' + hostname + '\n')
      try:
        # Get the IP address for a hostname
        address = socket.gethostbyname(hostname)
        # Connect to the origin server
        # ~~~~ INSERT CODE ~~~~
        originServerSocket.connect((address, 80))
        # ~~~~ END CODE INSERT ~~~~
        print ('Connected to origin Server')

        originServerRequest = ''
        originServerRequestHeader = ''
        # Create origin server request line and headers to send
        # and store in originServerRequestHeader and originServerRequest
        # originServerRequest is the first line in the request and
        # originServerRequestHeader is the second line in the request
        # ~~~~ INSERT CODE ~~~~
        originServerRequest = f"GET {resource} HTTP/1.1"
        originServerRequestHeader = f"Host: {hostname}\r\nConnection: close"
        # ~~~~ END CODE INSERT ~~~~

        # Construct the request to send to the origin server
        request = originServerRequest + '\r\n' + originServerRequestHeader + '\r\n\r\n'

        # Request the web resource from origin server
        print ('Forwarding request to origin server:')
        for line in request.split('\r\n'):
          print ('> ' + line)

        try:
          originServerSocket.sendall(request.encode())
        except socket.error:
          print ('Forward request to origin failed')
          sys.exit()

        print('Request sent to origin server\n')

        # Get the response from the origin server
        # ~~~~ INSERT CODE ~~~~
        response = b''
        while True:
          data = originServerSocket.recv(BUFFER_SIZE)
          if not data:
              break
          response += data
        # ~~~~ END CODE INSERT ~~~~


        #step 9
        parsed_response = parse_http_response(response)
        status_code = parsed_response['status_code']
        headers = parsed_response['headers']

        if status_code in ['301', '302']:
          if 'location' in headers:
              redirect_url = headers['location']
              print(f"Detected {status_code} redirect to: {redirect_url}")

        cache_time = -1

        if 'cache-control' in headers:
            cache_control = headers['cache-control']
            if 'max-age=' in cache_control:
                try:
                    cache_time = int(re.search(r'max-age=(\d+)', cache_control).group(1))
                    print(f"Detected max-age: {cache_time}")
                except:
                    pass


        # Send the response to the client
        # ~~~~ INSERT CODE ~~~~
        clientSocket.sendall(response)
        # ~~~~ END CODE INSERT ~~~~

        # Create a new file in the cache for the requested file.
        cacheDir, file = os.path.split(cacheLocation)
        print ('cached directory ' + cacheDir)
        if not os.path.exists(cacheDir):
          os.makedirs(cacheDir)
        cacheFile = open(cacheLocation, 'wb')

        if 'cache-control' in parsed_response['headers'] and 'max-age=' in parsed_response['headers']['cache-control']:
          try:
              max_age = int(re.search(r'max-age=(\d+)', parsed_response['headers']['cache-control']).group(1))
              cacheFile.write(f"X-Cache-Control: max-age={max_age}\r\n".encode())
              print(f"Added cache control: max-age={max_age}")
          except:
              pass

        # Save origin server response in the cache file
        # ~~~~ INSERT CODE ~~~~
        cacheFile.write(response)
        # ~~~~ END CODE INSERT ~~~~
        cacheFile.close()
        print ('cache file closed')

        # finished communicating with origin server - shutdown socket writes
        print ('origin response received. Closing sockets')
        originServerSocket.close()
        
        clientSocket.shutdown(socket.SHUT_WR)
        print ('client socket shutdown for writing')
      except OSError as err:
        print ('origin server request failed. ' + err.strerror)

    try:
      clientSocket.close()
    except:
      print ('Failed to close client socket')