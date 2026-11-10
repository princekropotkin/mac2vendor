/*
Copyright 2026, Prince Kropotkin <haiku@kropotk.in>
All rights reserved. Distributed under the terms of the MIT license.
mac2vendor: prints the vendor name for a MAC address.

Build:
g++ mac2vendor.cpp -o mac2vendor -O3

Usage: mac2vendor [-d database.m2v] [MAC ...]
Without MAC arguments, MACs are read from stdin, one per line.

Output: one line per MAC on stdout, containing only the vendor name
(an empty line if the vendor is unknown or the MAC is invalid, so the
lines always match the input). Diagnostics go to stderr.
Exit code: 0 = all found, 1 = some vendor not found, 2 = bad input/database.

Accepted MAC forms: aa:bb:cc:dd:ee:ff, aa-bb-cc-dd-ee-ff, aabb.ccdd.eeff,
aabbccddeeff, or just a prefix of at least 6 hex digits. */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <string>

static const char* kDefaultDb = "/boot/system/non-packaged/data/mac2vendor/oui.m2v";

// Table index: 0 = MA-L (6 hex digits), 1 = MA-M (7), 2 = MA-S (9)
static const int kDigits[3] = { 6, 7, 9 };
static const int kKeyBytes[3] = { 4, 4, 8 };

struct Db {
	const uint8_t* offsets;
	const uint8_t* pool;
	const uint8_t* keys[3];
	const uint8_t* ids[3];
	uint32_t vendorCount;
	uint32_t poolSize;
	uint32_t count[3];
};

static uint64_t Get(const uint8_t* p, int bytes) {
	uint64_t v = 0;
	for (int i = bytes - 1; i >= 0; i--)
		v = (v << 8) | p[i];
	return v;
}

static bool OpenDb(const char* path, Db& db) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "cannot open %s\n", path);
		return false;}
	struct stat st;
	if (fstat(fd, &st) != 0 || st.st_size < 24) {
		fprintf(stderr, "bad database %s\n", path);
		close(fd);
		return false;}
	size_t size = (size_t)st.st_size;
	void* map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) {
		fprintf(stderr, "cannot map %s\n", path);
		return false;}
	const uint8_t* base = (const uint8_t*)map;
	if (memcmp(base, "M2V1", 4) != 0) {
		fprintf(stderr, "%s: not an M2V1 database\n", path);
		return false;
	}
	db.vendorCount = (uint32_t)Get(base + 4, 4);
	db.poolSize = (uint32_t)Get(base + 8, 4);
	for (int t = 0; t < 3; t++)
		db.count[t] = (uint32_t)Get(base + 12 + 4 * t, 4);

	uint64_t pos = 24;
	db.offsets = base + pos;
	pos += 4ULL * db.vendorCount;
	for (int t = 0; t < 3; t++) {
		db.keys[t] = base + pos;
		pos += (uint64_t)kKeyBytes[t] * db.count[t];
		db.ids[t] = base + pos;
		pos += 2ULL * db.count[t];}
	db.pool = base + pos;
	if (pos + db.poolSize != size) {
		fprintf(stderr, "%s: size mismatch, file is damaged\n", path);
		return false;}
	return true;
}

static bool ParseMac(const char* s, std::string& digits) {
	digits.clear();
	for (; *s != '\0'; s++) {
		char c = *s;
		if (c == ':' || c == '-' || c == '.' || c == ' ' || c == '\t' || c == '\r' || c == '\n')
			continue;
		if (c >= 'a' && c <= 'f')	c -= 'a' - 'A';
		if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
			return false;
		digits += c;}
	return digits.size() >= 6 && digits.size() <= 12;
}

static uint64_t HexValue(const std::string& s, size_t len) {
	uint64_t v = 0;
	for (size_t i = 0; i < len; i++) {
		char c = s[i];
		v = (v << 4) | (uint64_t)(c <= '9' ? c - '0' : c - 'A' + 10);
	}
	return v;
}

static long Find(const Db& db, int t, uint64_t key) {
	long lo = 0, hi = (long)db.count[t] - 1;
	while (lo <= hi) {
		long mid = lo + (hi - lo) / 2;
		uint64_t k = Get(db.keys[t] + (size_t)kKeyBytes[t] * mid, kKeyBytes[t]);
		if (k == key)	return mid;
		if (k < key)	lo = mid + 1;
		else			hi = mid - 1;}
	return -1;
}

// Longest prefix first: MA-S, then MA-M, then MA-L.
static const char* Lookup(const Db& db, const std::string& digits) {
	for (int t = 2; t >= 0; t--) {
		if ((int)digits.size() < kDigits[t])	continue;
		long i = Find(db, t, HexValue(digits, kDigits[t]));
		if (i < 0)	continue;
		uint64_t id = Get(db.ids[t] + 2 * i, 2);
		if (id >= db.vendorCount)	return NULL;
		uint64_t off = Get(db.offsets + 4 * id, 4);
		if (off >= db.poolSize || memchr(db.pool + off, 0, db.poolSize - off) == NULL)
			return NULL;
		return (const char*)db.pool + off;}
	return NULL;
}

// Returns 0 = found, 1 = not found, 2 = invalid MAC.
static int Process(const Db& db, const char* mac) {
	std::string digits;
	if (!ParseMac(mac, digits)) {
		fprintf(stderr, "invalid MAC: %s\n", mac);
		printf("\n");
		return 2;}
	const char* name = Lookup(db, digits);
	if (name == NULL) {
		printf("\n");
		return 1;}
	for (const char* p = name; *p != '\0'; p++)
		putchar((unsigned char)*p < 0x20 ? ' ' : *p);
	putchar('\n');
	return 0;
}

int main(int argc, char** argv) {
	const char* dbPath = kDefaultDb;
	int i= 1;
	if (i + 1 < argc && strcmp(argv[i], "-d") == 0) {
		dbPath = argv[i + 1];
		i += 2;}
	if (i < argc && strcmp(argv[i], "-h") == 0) {
		fprintf(stderr, "usage: %s [-d database.m2v] [MAC ...]\n", argv[0]);
		return 2;}

	Db db;
	if (!OpenDb(dbPath, db))	return 2;

	int result = 0;
	if (i < argc) {
		for (; i < argc; i++)
			result = std::max(result, Process(db, argv[i]));
	} else {
		char line[256];
		while (fgets(line, sizeof(line), stdin) != NULL) {
			if (line[strspn(line, " \t\r\n")] == '\0')
				continue;
			result = std::max(result, Process(db, line));
		}
	}
	return result;
}
