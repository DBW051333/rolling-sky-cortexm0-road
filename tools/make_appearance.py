"""Compile an independent SD artwork pack and small flat-color ROM fallback.
PNG pixels are converted only to native RGB565. Models are data, not collisions.
"""
import json, struct, zlib
from pathlib import Path
from PIL import Image
base=Path(__file__).resolve().parents[1]
obj=json.loads((base/'levels/appearance.json').read_text())
color_fields=('background','light','dark','grid','pad','fragile','fragile_edge','fragile_fall_light','fragile_fall_dark','barrier_front','barrier_side','barrier_top','solid_front','solid_side','solid_roof','solid_inset','trunk_front','trunk_side','trunk_top','leaf_front','leaf_side','leaf_top','short_pad','air_front','air_side','air_top')
fields=color_fields+('solid_model','backdrop','air_model','ground_model')
lines=['/* Small flat-color fallback; detailed geometry/background come from SD. */','static const Appearance appearances[]={']
names=[p['name'] for p in obj['palettes']]
pack=bytearray(64);pack[:8]=b'RSART002'
def append(data):
 if len(pack)&1:pack.append(0)
 pos=len(pack);pack.extend(data);return pos
look_data=b''.join(struct.pack('<26H4B',*(p[k] for k in fields)) for p in obj['palettes'])
look_offset=append(look_data)
for p in obj['palettes']:lines.append('    {'+','.join(str(p[k]) for k in fields)+'},')
lines.extend(['};','static const AppearanceCue appearance_cues[]={'])
cue_data=bytearray()
for c in obj['bindings']:
 vals=(c['level_id'],c['first_row'],c['end_row'],names.index(c['palette']))
 if not 0<=vals[1]<vals[2]<=65535:raise ValueError('Invalid appearance cue')
 lines.append('    {'+','.join(map(str,vals))+'},')
 cue_data.extend(struct.pack('<3HBB',*vals,0))
lines.extend(['};',''])
(base/'firmware/appearance_data.h').write_text('\n'.join(lines),encoding='ascii')
cue_offset=append(cue_data)
models=bytearray()
for m in obj['models']:
 boxes=m['boxes']
 if not 1<=len(boxes)<=4:raise ValueError('Models need 1..4 boxes')
 if sum(3+bool(b['flags']&1) for b in boxes)>9:raise ValueError('Keep each model within the 9-face geometry budget')
 models.extend(struct.pack('<HH',len(boxes),0))
 for b in boxes:
  bounds=b['bounds'];colors=[color_fields.index(k) for k in b['colors']]
  if len(bounds)!=6 or not all(0<=v<=512 for v in bounds) or b['flags']&~7:raise ValueError('Invalid model box')
  models.extend(struct.pack('<6h4B',*bounds,*colors,b['flags']))
 models.extend(bytes((4-len(boxes))*16))
for preset in obj['palettes']:
 for role,budget in (('solid_model',9),('air_model',3),('ground_model',4)):
  selected=obj['models'][preset[role]]
  if sum(3+bool(b['flags']&1) for b in selected['boxes'])>budget:
   raise ValueError(f'{role} exceeds its renderer face budget')
model_offset=append(models)
# The bitmap panorama is gone: road.c builds each scene's sky, silhouette and
# void colour from a ramp plus a height profile, so 2.7 kB of cache no longer
# buys four copies of the same hillside. Header slots 20..28 stay reserved and
# are written as zero; appearance.c rejects anything that still fills them.
size=len(pack)
struct.pack_into('<II',pack,8,size,zlib.crc32(pack[64:]))
struct.pack_into('<13H',pack,16,0,144,0,0,0,0,0,look_offset,len(obj['palettes']),
                 cue_offset,len(obj['bindings']),model_offset,len(obj['models']))
pack.extend(bytes((-len(pack))%512))
if len(pack)>4096:raise ValueError('Artwork exceeds the 4 KiB reserved cache')
target=base/'sd_payload/RSART.BIN';target.parent.mkdir(exist_ok=True);target.write_bytes(pack)
print(f'SD art: {size} data bytes, {len(pack)} padded bytes. Palettes + model geometry; scene backdrops are generated in firmware.')
