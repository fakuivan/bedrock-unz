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

There should be no issue with this as long as the server is compiled with the same
compression algorithms provided by the version of leveldb used here. My understanding
is that [leveldb stores the compression algo used for the page so that it can read
it regardless of it's uncompressed](
https://github.com/Amulet-Team/leveldb-mcpe/blob/c446a37734d5480d4ddbc371595e7af5123c4925/table/format.cc#L132
), or compressed with some algo.

Once open however the server is free to compress whatever page it wants.

### Behavior so far

* No data is restored if the DB is put back into a save without compression
* If the DB is compressed back, then some data like entities is preserved, but chunks
  are not restored

In principle, unless Bedrock is integrated more deeply into leveldb than what is
presented in the API, all we're doing here should work. This could be due to version
issues or that the branch used here isn't actually the one used in bedrock.

The same behavior is observed when using the leveldb branch used by Amulet.

Next step is to try this on a dedicated server to try to get more detailed logs:
the bedrock server throws zero errors, it just fails silently and regenerates the
chunks.

Turns out that if you open a DB without the required compressors configured, leveldb
says "it's all good" and just keeps on going. This will result on the program ignoring
the compressed blocks, even when `paranoid_checks` is set. This does not look like it's
what's causing the issue however. Maybe try a round conversion for a very minimalist
word and analyze the blocks by hand?

I was not able to detect any issues when running a compaction on a db created by bedrock.
Given that ldb failed silently on me once we're going into defensive mode, try:

* Grab a db, deleting all of its contents, and cloning to it
* It might be the creation part that is causing issues.
  ldb has the `->Delete` method, maybe try that?
* Cloning in reverse, will it give the same results?
* Grab an empty db created by us, then a purged one created by bedrock and compare
