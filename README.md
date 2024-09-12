# bedrock-unz

Removes and adds compression to a leveldb database. Compiled using mojang's fork of leveldb.

## Dev notes

### Building

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
build main
```

TODO Make a docker image to build the project.

### Why we create a new DB

leveldb does not expose any functions to remove compression or change it back once it
back once the pages reach a certain compression ratio. So the only alternative we have
besides hacking the implementation is to create a new uncompressed DB and copy all of
the key-value pairs from the compressed one.

### Giving the server an uncompressed DB

There should be no issue with this as long as the server is compiled with the same
compression algorithms provided by the version of leveldb used here. My understanding
is that the leveldb stores the compression algo used for the page so that it can read
it regardless of it's uncompressed, or compressed with some algo.

Once open however the server is free to compress whatever page it wants.

### Behavior so far

* No data is restored if the DB is put back into a save without compression
* If the DB is compressed back, then some data like entities is preserved, but chunks
  are not restored

In principle, unless Bedrock is integrated more deeply into leveldb than what is
presented in the API, all we're doing here should work. This could be due to version
issues or that the branch used here isn't actually the one used in bedrock.

TODO would be to test different branches of leveldb and see what works.
