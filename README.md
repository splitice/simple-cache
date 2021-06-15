simple-cache
============

[![Build Status](https://travis-ci.org/splitice/simple-cache.svg?branch=master)](https://travis-ci.org/splitice/simple-cache)

A simple on disk small / large key value store with LRU. Now also includes a monitoring API.

## Building
Compile with the provided Makefile. Requires a GCC version with C++11 support. Tested with gcc-4.8.

## General
**Features:**
 - One daemon controls multiple tables of key-values. The table is specified by the first path element in a request (table)
 - Valid HTTP/1.1 response including Keepalive and Content-Length
 - Accept multiple requests per connection (keepalive)
 - Non-blocking socket IO, Single threaded
 - Stream values small blocks) and large (single files) to the socket using sendfile or similar
 - LRU support (global size limit)
 - Murmurhash3 hashing algorithm, with exact key check for hash collisions
  
## Requests

### Cache: Get Key Value
**GET /table/key HTTP/1.1**

Get the value of key in table
```
curl http://127.0.0.1:8000/table/key -XGET -v
```
**Key Not Found:**
```
$ curl http://127.0.0.1:8000/table/key -XGET -v
* Hostname was NOT found in DNS cache
*   Trying 127.0.0.1...
* Connected to 127.0.0.1 (127.0.0.1) port 8000 (#0)
> GET /table/key HTTP/1.1
> User-Agent: curl/7.35.0
> Host: 127.0.0.1:8000
> Accept: */*
>
< HTTP/1.1 404 File Not Found
< Connection: Keep-Alive
< Content-Length: 15
<
Key not Found
* Connection #0 to host 127.0.0.1 left intact
```
**Key Found:**
```
$ curl http://127.0.0.1:8000/table/key -XGET -v
* Hostname was NOT found in DNS cache
*   Trying 127.0.0.1...
* Connected to 127.0.0.1 (127.0.0.1) port 8000 (#0)
> GET /table/key HTTP/1.1
> User-Agent: curl/7.35.0
> Host: 127.0.0.1:8000
> Accept: */*
>
< HTTP/1.1 200 OK
< Connection: Keep-Alive
< X-Ttl: 0
< Content-Length: 9
<
* Connection #0 to host 127.0.0.1 left intact
key value
```

## Cache: Insert or Update Key
**PUT /table/key HTTP/1.1**

Put (Set or inset) a value (post data) into key in table
```
curl http://127.0.0.1:8000/table/key -XPUT -v -d 'key value'
```
```
$ curl http://127.0.0.1:8000/table/key -XPUT -v -d 'key value'
* Hostname was NOT found in DNS cache
*   Trying 127.0.0.1...
* Connected to 127.0.0.1 (127.0.0.1) port 8000 (#0)
> PUT /table/key HTTP/1.1
> User-Agent: curl/7.35.0
> Host: 127.0.0.1:8000
> Accept: */*
> Content-Length: 9
> Content-Type: application/x-www-form-urlencoded
>
* upload completely sent off: 9 out of 9 bytes
< HTTP/1.1 200 OK
< Connection: Keep-Alive
< Content-Length: 4
<
OK
* Connection #0 to host 127.0.0.1 left intact
```

## Cache: Delete Key
**DELETE /table/key HTTP/1.1**

Delete the key in table
```
curl http://127.0.0.1:8000/table/key -XDELETE -v
```
```
$ curl http://127.0.0.1:8000/table/key -XDELETE -v
* Hostname was NOT found in DNS cache
*   Trying 127.0.0.1...
* Connected to 127.0.0.1 (127.0.0.1) port 8000 (#0)
> DELETE /table/key HTTP/1.1
> User-Agent: curl/7.35.0
> Host: 127.0.0.1:8000
> Accept: */*
>
< HTTP/1.1 200 OK
< Connection: Keep-Alive
< Content-Length: 9
<
DELETED
* Connection #0 to host 127.0.0.1 left intact
```

## Cache: Delete / Purge table
**DELETE /table HTTP/1.1**

Delete table, effectively purging table
```
curl http://127.0.0.1:8000/table -XDELETE -v
```
**Table Not Found:**
```
$ curl http://127.0.0.1:8000/table -XDELETE -v
* Hostname was NOT found in DNS cache
*   Trying 127.0.0.1...
* Connected to 127.0.0.1 (127.0.0.1) port 8000 (#0)
> DELETE /table HTTP/1.1
> User-Agent: curl/7.35.0
> Host: 127.0.0.1:8000
> Accept: */*
>
< HTTP/1.1 404 File Not Found
< Connection: Keep-Alive
< Content-Length: 15
<
Key not Found
* Connection #0 to host 127.0.0.1 left intact
```
**Table Found:**
```
$ curl http://127.0.0.1:8000/table -XDELETE -v
* Hostname was NOT found in DNS cache
*   Trying 127.0.0.1...
* Connected to 127.0.0.1 (127.0.0.1) port 8000 (#0)
> DELETE /table HTTP/1.1
> User-Agent: curl/7.35.0
> Host: 127.0.0.1:8000
> Accept: */*
>
< HTTP/1.1 200 OK
< Connection: Keep-Alive
< Content-Length: 9
<
DELETED
* Connection #0 to host 127.0.0.1 left intact
```

## Cache: List table contents
**GET /table HTTP/1.1**

Get a listing of keys in the table. NOTE: This may return duplicates
```
curl http://127.0.0.1:8000/table -XGET -v
```
**Table Not Found:**
```
$ curl http://127.0.0.1:8000/table -XGET -v
* Hostname was NOT found in DNS cache
*   Trying 127.0.0.1...
* Connected to 127.0.0.1 (127.0.0.1) port 8000 (#0)
> GET /table HTTP/1.1
> User-Agent: curl/7.35.0
> Host: 127.0.0.1:8000
> Accept: */*
>
< HTTP/1.1 404 File Not Found
< Connection: Keep-Alive
< Content-Length: 15
<
Key not Found
* Connection #0 to host 127.0.0.1 left intact
```
**Table Found:**
```
$ curl http://127.0.0.1:8000/table -XGET -v
* Hostname was NOT found in DNS cache
*   Trying 127.0.0.1...
* Connected to 127.0.0.1 (127.0.0.1) port 8000 (#0)
> GET /table HTTP/1.1
> User-Agent: curl/7.35.0
> Host: 127.0.0.1:8000
> Accept: */*
>
< HTTP/1.1 200 OK
< Connection: Close
< X-Entries: 1
< X-Total: 4
<
key
* Closing connection 0
```

**Cache: Request headers supported:**

GET (key): 
 - None

GET (list): 
 - X-Start: Offset to start looking at
 - X-Limit: Maximum keys to return (will return less than or equal)

PUT:
 - X-Ttl: Time in seconds this entry is valid for
 - Content-Length (required): Size of POST data

DELETE:
 - None

**Monitoring: Get root page (Also available as HEAD):**
**GET / HTTP/1.1**

### Block File
```
-----------------------------------------
|     Block 1    |     Block 2    |
|  (4096 bytes)  |  (4096 bytes)  |  ...
-----------------------------------------
```
Accessing a block is as simple as taking the block identifier (a 0 based number) and multiplying by the block length.

The block file will be expanded as needed. Up to a maximum of of uint_32 (4,294,967,295) blocks.

**Future:** Defrag / Optimize routine?  Possibly include a header structure for file version and block size?

### Filesystem
```
|
|- blockfile.db
|-aa/
|-aa/badfookpsokdfs.key
|-aa/asdsadasdasdas.key
|-ab/
| ...
```
The block data is contained within blockfile.db. Additional large keys are contained as individual filesystem items. A two identifier value is extracted from the key and used to partition key data between multiple folders. This prevents filesystem issues with large numbers of files in a single directory.

## LRU
A Least Recently Used list will be maintained globally. This will be done via a linked list between cache elements, and a pointer maintained to both the least recently used (head) and most recently used (tail).

Hard limits will be enforced on database size (sum of value size). When this limit is exceeded the least recently used values will be expired until the database it atleast X% under the limit.

## Expiration
Expiration is both lazy and implemented via a simple expiration cursor expiring a small number of tables at a time.

## Configuration
```
database-max-size: maximum size of database on disk
database-file-path: directory to store the database
database-lru-clear: percent of database to clear to make room during a LRU cleanup
bind-port: port to bind to
bind-addr: ip address to bind to
```

Configuration variables are supplied as command line arguments, for more information execute scache with ```--help```