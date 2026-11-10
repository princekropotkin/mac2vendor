# mac2vendor

Command-line tool for BeOS, Haiku or any POSIX system that prints the vendor
name for a MAC address, using the IEEE OUI registry in a own compact binary database.

The output is easy to parse: one line per MAC, containing only the vendor name.

## Components

- `mac2vendor` — the lookup tool (memory-maps the database, binary search).
- `mac2vendor.convert` — converts the IEEE CSV files into the compact `.m2v` database.

## Build

    make

## Create the database

Download the three IEEE registry files (MA-L, MA-M, MA-S) into the project directory:

    ./getoui

Then:

    make db

or directly:

    ./mac2vendor.convert oui.m2v oui.csv mam.csv oui36.csv

The converter picks the table by assignment length (6 hex digits = MA-L,
7 = MA-M, 9 = MA-S), so the order of the input files does not matter.
Only the `Assignment` and `Organization Name` columns are used. It prints the
number of vendors and the size of every part of the database.

The CSV files and the database are not part of this repository.

## Usage

    mac2vendor [-d database.m2v] [MAC ...]

Without MAC arguments, MACs are read from stdin, one per line.

    $ mac2vendor -d oui.m2v 28:6F:B9:11:22:33
    Nokia Shanghai Bell Co., Ltd.

Accepted forms: `aa:bb:cc:dd:ee:ff`, `aa-bb-cc-dd-ee-ff`, `aabb.ccdd.eeff`,
`aabbccddeeff`, or a prefix of at least 6 hex digits. The longest matching
prefix wins (MA-S, then MA-M, then MA-L).

Output:

- one line on stdout per MAC, containing only the vendor name;
- an empty line if the vendor is unknown or the MAC is invalid, so the output
  lines always match the input lines;
- diagnostics go to stderr.

Exit code: `0` all found, `1` some vendor not found, `2` invalid MAC or bad database.

The default database path is
`/boot/system/non-packaged/data/mac2vendor/oui.m2v`; use `-d` to override it.
`make install install-db` installs the tool and the database under `PREFIX`
(default `/boot/system/non-packaged`).

## Database format (M2V1)

All values are little-endian.

    "M2V1"
    uint32 vendorCount, poolSize, countL, countM, countS
    uint32 vendorOffset[vendorCount]        offsets into the string pool
    MA-L:  uint32 key[countL], uint16 vendor[countL]
    MA-M:  uint32 key[countM], uint16 vendor[countM]
    MA-S:  uint64 key[countS], uint16 vendor[countS]
    string pool: NUL-terminated vendor names

Keys are sorted ascending. Every vendor name is stored once, and prefixes refer
to it by a 16-bit id, which makes the ≈5 MB file -> ≈1.2 MB (and ≈400 KB after further
compression with xz).

## Data source

The registry data comes from the IEEE Registration Authority
(https://standards.ieee.org/products-programs/regauth/). It's not included
into this repository.

## License

MIT, see `LICENSE`.
