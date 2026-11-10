/*
Copyright 2026, Prince Kropotkin <haiku@kropotk.in>
All rights reserved. Distributed under the terms of the MIT license.

mac2vendor.convert: converts IEEE OUI CSV files (MA-L, MA-M, MA-S) into a
compact binary database

Build:
g++ mac2vendor.convert.cpp -o mac2vendor.convert -O3

Usage: mac2vendor_conv out.m2v oui.csv mam.csv oui36.csv
The table is chosen by the assignment length: 6 hex = MA-L, 7 = MA-M, 9 = MA-S.

Output layout (all values little-endian):
   "M2V1"
   uint32 vendorCount, poolSize, countL, countM, countS
   uint32 vendorOffset[vendorCount]       (offsets into the string pool)
   MA-L: uint32 key[countL],  uint16 vendor[countL]
   MA-M: uint32 key[countM],  uint16 vendor[countM]
   MA-S: uint64 key[countS],  uint16 vendor[countS]
   string pool: NUL-terminated vendor names
Keys are sorted ascending, so lookup is a binary search. */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

struct Entry {
	uint64_t key;
	uint16_t vendor;
};

static std::vector<std::string> sNames;
static std::unordered_map<std::string, uint16_t> sNameIds;
static std::vector<Entry> sTables[3];	// 0 = MA-L, 1 = MA-M, 2 = MA-S

static bool ReadFile(const char* path, std::string& out) {
	FILE* f = fopen(path, "rb");
	if (f == NULL)	return false;
	char buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)	out.append(buf, n);
	fclose(f);
	return true;
}

// Reads one CSV record (quotes, "" escapes, newlines inside quotes).
static bool NextRecord(const std::string& d, size_t& pos, std::vector<std::string>& fields) {
	if (pos >= d.size())	return false;
	fields.clear();
	std::string field;
	bool quoted = false;
	while (pos < d.size()) {
		char c = d[pos++];
		if (quoted) {
			if (c != '"')	field += c;
			else if (pos < d.size() && d[pos] == '"') {
				field += '"';
				pos++;
			} else	quoted = false;
		} else if (c == '"')	quoted = true;
		else if (c == ',') {
			fields.push_back(field);
			field.clear();
		} else if (c == '\n')	break;
		else if (c != '\r')		field += c;
	}
	fields.push_back(field);
	return true;
}

static bool ParseHex(const std::string& s, uint64_t& v) {
	v = 0;
	for (size_t i = 0; i < s.size(); i++) {
		char c = s[i];
		int d;
		if (c >= '0' && c <= '9')	d = c - '0';
		else if (c >= 'A' && c <= 'F')	d = c - 'A' + 10;
		else if (c >= 'a' && c <= 'f')	d = c - 'a' + 10;
		else	return false;
		v = (v << 4) | d;
	}
	return true;
}

static std::string Trim(const std::string& s) {
	size_t b = s.find_first_not_of(" \t");
	if (b == std::string::npos)	return "";
	size_t e = s.find_last_not_of(" \t");
	return s.substr(b, e - b + 1);
}

static bool VendorId(const std::string& name, uint16_t& id) {
	std::unordered_map<std::string, uint16_t>::iterator it = sNameIds.find(name);
	if (it != sNameIds.end()) {
		id = it->second;
		return true;
	}
	if (sNames.size() >= 65535)	return false;
	id = (uint16_t)sNames.size();
	sNames.push_back(name);
	sNameIds[name] = id;
	return true;
}

static bool ProcessFile(const char* path) {
	std::string data;
	if (!ReadFile(path, data)) {
		fprintf(stderr, "cannot read %s\n", path);
		return false;}
	size_t pos = (data.compare(0, 3, "\xEF\xBB\xBF") == 0) ? 3 : 0;
	std::vector<std::string> fields;
	int added = 0;
	while (NextRecord(data, pos, fields)) {
		if (fields.size() < 3)
			continue;
		// fields[0] = Registry, [1] = Assignment, [2] = Organization Name,
		// [3] = Organization Address (ignored)
		size_t len = fields[1].size();
		int table = (len == 6) ? 0 : (len == 7) ? 1 : (len == 9) ? 2 : -1;
		uint64_t key;
		if(table< 0 || !ParseHex(fields[1], key))	continue;// header or junk
		std::string name = Trim(fields[2]);
		if (name.empty())	continue;
		Entry e;
		e.key = key;
		if (!VendorId(name, e.vendor)) {
			fprintf(stderr, "too many vendors (>65535)\n");
			return false;}
		sTables[table].push_back(e);
		added++;}
	printf("%s: %d records\n", path, added);
	return true;
}

static void Put(std::string& out, uint64_t v, int bytes) {
	for (int i= 0; i < bytes; i++)	out += (char)(v >> (8 * i));
}

static bool EntryLess(const Entry& a, const Entry& b) {
	return a.key < b.key;
}

static bool EntryEqual(const Entry& a, const Entry& b) {
	return a.key== b.key;
}

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s out.m2v oui.csv [mam.csv oui36.csv ...]\n", argv[0]);
		return 1;}
	for (int i = 2; i < argc; i++) {
		if (!ProcessFile(argv[i]))	return 1;}

	for (int t = 0; t < 3; t++) {
		std::stable_sort(sTables[t].begin(), sTables[t].end(), EntryLess);
		size_t before = sTables[t].size();
		sTables[t].erase(std::unique(sTables[t].begin(), sTables[t].end(), EntryEqual),
			sTables[t].end());
		if (sTables[t].size() != before)
			fprintf(stderr, "table %d: %lu duplicate prefixes dropped\n", t,
				(unsigned long)(before - sTables[t].size()));
	}

	std::string pool;
	std::vector<uint32_t> offsets;
	for (size_t i = 0; i < sNames.size(); i++) {
		offsets.push_back((uint32_t)pool.size());
		pool+= sNames[i];
		pool+= '\0';}

	std::string out = "M2V1";
	Put(out, sNames.size(), 4);
	Put(out, pool.size(), 4);
	for (int t= 0; t< 3; t++)	Put(out, sTables[t].size(), 4);
	size_t headerSize= out.size();
	for (size_t i= 0; i < offsets.size(); i++)
		Put(out, offsets[i], 4);
	size_t vendorsSize= out.size() - headerSize;

	size_t tableSize[3];
	for (int t = 0; t < 3; t++) {
		size_t start = out.size();
		int keyBytes = (t == 2) ? 8 : 4;
		for (size_t i = 0; i < sTables[t].size(); i++)
			Put(out, sTables[t][i].key, keyBytes);
		for (size_t i = 0; i < sTables[t].size(); i++)
			Put(out, sTables[t][i].vendor, 2);
		tableSize[t] = out.size() - start;}
	out+= pool;

	FILE* f = fopen(argv[1], "wb");
	if (f == NULL || fwrite(out.data(), 1, out.size(), f) != out.size()) {
		fprintf(stderr, "cannot write %s\n", argv[1]);
		return 1;
	}
	fclose(f);

	printf("vendors: %lu\n", (unsigned long)sNames.size());
	printf("header:  %lu bytes\n", (unsigned long)headerSize);
	printf("vendor offsets: %lu bytes\n", (unsigned long)vendorsSize);
	printf("MA-L: %lu entries, %lu bytes\n", (unsigned long)sTables[0].size(),
		(unsigned long)tableSize[0]);
	printf("MA-M: %lu entries, %lu bytes\n", (unsigned long)sTables[1].size(),
		(unsigned long)tableSize[1]);
	printf("MA-S: %lu entries, %lu bytes\n", (unsigned long)sTables[2].size(),
		(unsigned long)tableSize[2]);
	printf("string pool: %lu bytes\n", (unsigned long)pool.size());
	printf("total: %lu bytes\n", (unsigned long)out.size());
	return 0;
}
