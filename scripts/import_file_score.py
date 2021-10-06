#!/usr/bin/env python3

from collections import namedtuple
from decimal import Decimal
import argparse
import os.path
import re
import sqlite3
import sys

def chunks(l, n):
	for i in range(0, len(l), n):
		yield l[i:i+n]

class Record(namedtuple('Record', 'name time checkpoints')):
	@staticmethod
	def parse(lines):
		if len(lines) != 3:
			raise ValueError("wrong amount of lines for record")
		name = lines[0]
		time = Decimal(lines[1])
		checkpoints_str = lines[2].split(' ')
		if len(checkpoints_str) != 26 or checkpoints_str[25] != "":
			raise ValueError("wrong amount of checkpoint times: {}".format(len(checkpoints_str)))
		checkpoints_str = checkpoints_str[:25]
		checkpoints = tuple(Decimal(c) for c in checkpoints_str)
		return Record(name=name, time=time, checkpoints=checkpoints)

	def unparse(self):
		return "\n".join([self.name, str(self.time), " ".join([str(cp) for cp in self.checkpoints] + [""]), ""])

def read_records(file):
	contents = file.read().splitlines()
	return [Record.parse(c) for c in chunks(contents, 3)]

MAP_RE=re.compile(r"^(?P<map>.*)_record\.dtb$")
def main():
	p = argparse.ArgumentParser(description="Merge multiple DDNet race database files", formatter_class=argparse.ArgumentDefaultsHelpFormatter)
	p.add_argument("--out", default="ddnet-server.sqlite", help="Output SQLite database")
	p.add_argument("in_", metavar="IN", nargs='+', help="Text score databases to import; must have the format MAPNAME_record.dtb")
	p.add_argument("--dry-run", "-n", action='store_true', help="Don't write out the resulting SQLite database")
	p.add_argument("--stats", action='store_true', help="Display some stats at the end of the import process")
	args = p.parse_args()

	records = {}
	for in_ in args.in_:
		match = MAP_RE.match(os.path.basename(in_))
		if not match:
			raise ValueError("Invalid text score database name, does not end in '_record.dtb': {}".format(in_))
		m = match.group("map")
		if m in records:
			raise ValueError("Two text score databases refer to the same map: {}".format(in_))
		with open(in_) as f:
			records[m] = read_records(f)

	if not args.dry_run:
		conn = sqlite3.connect(args.out)
		c = conn.cursor()
		c.execute("CREATE TABLE IF NOT EXISTS record_race ("
			"Map VARCHAR(128) COLLATE BINARY NOT NULL, "
			"Name VARCHAR(16) COLLATE BINARY NOT NULL, "
			"Timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, "
			"Time FLOAT DEFAULT 0, "
			"Server CHAR(4), " +
			"".join("cp{} FLOAT DEFAULT 0, ".format(i + 1) for i in range(25)) +
			"GameID VARCHAR(64), "
			"DDNet7 BOOL DEFAULT FALSE"
		");")
		c.executemany(
			"INSERT INTO record_race (Map, Name, Time, Server, " +
			"".join("cp{}, ".format(i + 1) for i in range(25)) +
			"GameID, DDNet7) " +
			"VALUES ({})".format(",".join("?" * 31)),
			[(map, r.name, float(r.time), "TEXT", *[float(c) for c in r.checkpoints], None, False) for map in records for r in records[map]]
		)
		conn.commit()
		conn.close()

	if args.stats:
		print("Number of imported text databases: {}".format(len(records)), file=sys.stderr)
		print("Number of imported ranks: {}".format(sum(len(r) for r in records.values()), file=sys.stderr))

if __name__ == '__main__':
	sys.exit(main())
