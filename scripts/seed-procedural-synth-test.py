"""Create a fresh vanilla test DB, reusing only one existing local test identity.

The source database is opened read-only. Existing destination files are never replaced.
"""
import argparse
import sqlite3
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("destination", type=Path)
p.add_argument("--account", default="ass")
args = p.parse_args()
if args.destination.exists():
    print("Existing synth test database preserved.")
    raise SystemExit(0)
source = sqlite3.connect(args.source.resolve().as_uri() + "?mode=ro", uri=True)
source.row_factory = sqlite3.Row
account = source.execute("SELECT * FROM accounts WHERE username=?", (args.account,)).fetchone()
if account is None:
    raise SystemExit("Local test account was not found; supply --account.")
destination = sqlite3.connect(args.destination)
for row in source.execute("SELECT sql FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' AND sql IS NOT NULL"):
    destination.execute(row[0])
for row in source.execute("SELECT sql FROM sqlite_master WHERE type IN ('index','trigger') AND sql IS NOT NULL"):
    destination.execute(row[0])
def insert(table, row):
    columns = list(row.keys())
    destination.execute(f'INSERT INTO "{table}" ({",".join(columns)}) VALUES ({",".join("?" for _ in columns)})', list(row))
insert("accounts", account)
for key in source.execute("SELECT * FROM account_keypairs WHERE account_id=?", (account["id"],)):
    insert("account_keypairs", key)
for index, name in enumerate(("Synth A", "Synth B")):
    destination.execute("""INSERT INTO characters
        (id,account_id,name,cell,pos_x,pos_y,pos_z,is_new,race,head_mesh,hair_mesh,is_male,class_id,birth_sign)
        VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
        (1001+index,account["id"],name,"ToddTest",2175+index*100,2176,-700,0,"imperial",
         "b_n_imperial_m_head_01","b_n_imperial_m_hair_01",1,"warrior","elfborn"))
destination.execute("PRAGMA user_version=" + str(source.execute("PRAGMA user_version").fetchone()[0]))
destination.commit()
destination.close()
source.close()
print("Created isolated vanilla characters Synth A and Synth B.")
