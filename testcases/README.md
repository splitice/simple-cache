# Test case format

simple-cache (scache) uses a simple and basic unit testing language

Steps start with the connection number e.g `$connectionNumber>>>>>` (request on connection number). If that connection does not exist it will be opened.

The following instructions exist for steps within a test unit:

1. `>>>>>` "request": send data to scache
2. `-----` "expect response": expect to receive data from scache (MUST follow a request)
3. `----c` "expect close": expect scache to close the connection (MUST follow a request)
4. `*****[0-9]+` "delay": sleep for seconds
5. `sr*****` "shutdown": shutdown connection (read)
5. `sw*****` "shutdown": shutdown connection (write)
5. `sd*****` "shutdown": shutdown connection (duplex)
6. `c*****` "close": close the connection

A scenario (folder) consists of multiple units