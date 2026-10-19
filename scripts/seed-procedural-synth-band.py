"""Add one persistent second keyboard to the isolated stage without changing characters."""
import argparse
import datetime
from pathlib import Path
import sqlite3

p=argparse.ArgumentParser()
p.add_argument('database',type=Path)
a=p.parse_args()
if a.database.parent.name!='procedural-synth' or a.database.name!='playerdata.db':
    raise ValueError('Expected isolated procedural-synth/playerdata.db')
with sqlite3.connect(a.database) as db:
    if sorted(db.execute('select id,name from characters where id in (1001,1002)'))!=[(1001,'Synth A'),(1002,'Synth B')]:
        raise ValueError('Unexpected fixture characters')
    columns=[r[1] for r in db.execute('pragma table_info(world_objects)')]
    home=db.execute("select * from world_objects where mp_num=1 and ref_id='fetcher_procedural_synth_v1'").fetchone()
    if not home:
        raise ValueError('Place the stage keyboard before adding a second instrument')
    row=dict(zip(columns,home))
    existing=db.execute("select mp_num from world_objects where ref_id='fetcher_procedural_keyboard_v1' and cell_id=? and item_count>0 and abs(pos_x-?)<400 and abs(pos_y-?)<400",(row['cell_id'],row['pos_x'],row['pos_y'])).fetchone()
    if existing: print('Existing second stage keyboard preserved:',existing[0])
    else:
        backup=a.database.with_name('band-stage-backup-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'.db')
        with sqlite3.connect(backup) as out: db.backup(out)
        next_id=max(db.execute('select coalesce(max(mp_num),0)+1 from world_objects').fetchone()[0],
                    db.execute("select coalesce((select value from world_metadata where key='next_mp_num'),1)").fetchone()[0])
        row.update(mp_num=next_id,ref_id='fetcher_procedural_keyboard_v1',pos_x=row['pos_x']+180)
        db.execute('insert into world_objects values ('+','.join('?' for _ in columns)+')',[row[k] for k in columns])
        db.execute("insert or replace into world_metadata(key,value) values ('next_mp_num',?)",(next_id+1,))
        print('Added second stage keyboard:',next_id,'; original database:',backup)
