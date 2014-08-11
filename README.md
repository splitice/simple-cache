simple-cache
============

[![Build Status](https://travis-ci.org/splitice/simple-cache.svg?branch=master)](https://travis-ci.org/splitice/simple-cache)

A simple on disk small / large key value store with LRU

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

### Get Key Value
**GET /table/key HTTP/1.1**

Get the value of key in table
```
curl http://127.0.0.1:8000/table/key -XGET -v
```
```

```

## Insert or Update Key
**PUT /table/key HTTP/1.1**

Put (Set or inset) a value (post data) into key in table
```
curl http://127.0.0.1:8000/table/key -XPUT -v -d 'key value'
```
```

```

## Delete Key
**DELETE /table/key HTTP/1.1**

Delete the key in table
```
curl http://127.0.0.1:8000/table/key -XDELETE -v
```
```

```

## Delete / Purge table
**DELETE /table HTTP/1.1**

Delete table, effectively purging table
```
curl http://127.0.0.1:8000/table -XDELETE -v
```
```

```

## List table contents
**GET /table HTTP/1.1**

Get a listing of keys in the table. NOTE: This may return duplicates
```
curl http://127.0.0.1:8000/table -XGET -v
```
```

```

**Request headers supported:**

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

### Block File
```
-----------------------------------------
|     Block 1    |     Block 2    |
|  (4096 bytes)  |  (4096 bytes)  |  ...
-----------------------------------------
```
Accessing a block is as simple as taking the block identifier (a 0 based number) and multiplying by the block length. Possibly include a header structure for file version and block size?

The block file will be expanded as needed. Up to a maximum of of uint_32 (4,294,967,295) blocks.

**Future:** Defrag / Optimize routine?

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
For now expiration will be lazy, performed on the GET request as validation. In the future an expiration cursor will be implemented.

## Configuration
```
database-max-size: maximum size of database on disk
database-file-path: directory to store the database
database-lru-clear: percent of database to clear to make room during a LRU cleanup
bind-port: port to bind to
bind-addr: ip address to bind to
```

Configuration variables are supplied as command line arguments, for more information execute scache with ```--help```