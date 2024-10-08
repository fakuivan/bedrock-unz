# bedrock-unz

Removes and adds compression to a leveldb database. Compiled using mojang's fork of leveldb.

## Dev notes

### Building

First apply the [patches](./patches).

Most of the requirements besides `snappy` are included in this repo. In ubuntu snappy
can be installed with:

```bash
sudo apt install libsnappy-dev
```

Then compile with:

```bash
mkdir build
cd build
cmake ..
make main
```

TODO Make a docker image to build the project.

### Why we create a new DB

leveldb does not expose any functions to remove compression or change it back once it
back once the pages reach a certain compression ratio. So the only alternative we have
besides hacking the implementation is to create a new uncompressed DB and copy all of
the key-value pairs from the compressed one.

### Giving the server an uncompressed DB

Bedrock can use an uncompressed DB just fine, Once open however the server is free
to compress whatever page it wants.

### Behavior so far

Everything works as long as you use the keys as keys and values as values, and not
the other way around :P
