"""Reset only the disposable fixture's instrument and two test-character positions."""
import argparse
import datetime
from pathlib import Path
import sqlite3

p=argparse.ArgumentParser()
p.add_argument('database',type=Path)
args=p.parse_args()
if args.database.parent.name!='procedural-synth' or args.database.name!='playerdata.db':
    raise ValueError('Expected the isolated procedural-synth/playerdata.db')
c=sqlite3.connect(args.database)
characters=c.execute("select id,name from characters where id in (1001,1002)").fetchall()
if sorted(characters)!=[(1001,'Synth A'),(1002,'Synth B')]:
    raise ValueError('Unexpected fixture characters; database was not changed')
backup=args.database.with_name('stage-layout-backup-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'.db')
with sqlite3.connect(backup) as destination:c.backup(destination)
with c:
    c.execute("update world_objects set pos_x=2175,pos_y=2256,pos_z=-703,rot_x=0,rot_y=0,rot_z=0 where mp_num=1 and ref_id='fetcher_procedural_synth_v1'")
    c.execute("update characters set cell='ToddTest',pos_x=2175,pos_y=2212,pos_z=-703,rot_x=0,rot_y=0,rot_z=0 where id=1001")
    c.execute("update characters set cell='ToddTest',pos_x=2290,pos_y=2305,pos_z=-703,rot_x=0.15,rot_y=0,rot_z=-2.25 where id=1002")
print('Stage layout reset; prior fixture database backed up to',backup)
