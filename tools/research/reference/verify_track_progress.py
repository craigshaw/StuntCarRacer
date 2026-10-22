"""Pin original camera/grid/track-progress producers and audit enhanced-source changes."""
from pathlib import Path
import argparse
import hashlib
import json
import re
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_track_sampling import canonical as _canonical

ROOT=Path(__file__).resolve().parents[3];REF=ROOT/'analysis/reference/vesuri';OUT=ROOT/'analysis/track-progress'
BASE,DISK,SIZE=0xe700,0xdc00,408540
ADF_SHA='548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA='508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA='321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT='3cce78d193c3d0208be34d0bbfc3155e787cd406'
ROUTINES=[
 ('applyTrackSegmentGeometry',0x5be44,0x5c0aa,'getTrackDataPoint'),
 ('calculateTrackPosition',0x5c138,0x5c1d0,'updateWheelHeightsFromTrack'),
 ('applyTrackDirectionTransform',0x5c3da,0x5c484,'handleSegmentTransition'),
 ('advanceToNextSegment',0x5c51a,0x5c538,'retreatToPreviousSegment'),
 ('retreatToPreviousSegment',0x5c538,0x5c554,'calculateBilinearTrackInterpolation'),
 ('calculatePerspectiveAdjustment',0x5c65a,0x5c6b8,'interpolateOpponentTrackPositionFromData'),
 ('interpolateOpponentTrackPositionFromData',0x5c6c4,0x5c808,'checkLateralBoundsAndBounce'),
 ('getSegmentAtGridCoordinate',0x5fe04,0x5fe56,'loadTrackSegmentConfiguration'),
 ('loadTrackSegmentConfiguration',0x5fe56,0x5ff94,'transformSegmentToViewSpace'),
 ('transformSegmentToViewSpace',0x5ff94,0x6001c,'findSegmentBySubGridAdjustment'),
 ('findSegmentBySubGridAdjustment',0x6001c,0x600a6,'convertCameraToGridSpace'),
 ('convertCameraToGridSpace',0x600a6,0x60190,'calculateCameraTransform'),
 ('calculateCameraTransform',0x60190,0x60246,'checkRoadBoundaries'),
 ('checkRoadBoundaries',0x60246,0x602e4,'renderTrackPreviewGrid'),
 ('initializeDebrisParticlePositions',0x60cde,0x60cf8,'handleEdgeCollision'),
 ('squareAndAbsolute',0x6135a,0x61368,'calculateTransformMatrices'),
 ('calculateDivision',0x64d66,0x64de8,'calculateVectorMagnitude'),
 ('calculateVectorMagnitude',0x64de8,0x64e18,'prepareFrameRendering')]
PREFIX=[('processGameFrame_lookup_and_offgrid_prefix',0x64e52,0x64ede),('processGameFrame_found_prefix',0x64ef6,0x64f80)]
TABLES=[('angle_ratio_table',0x1cc46,0x1dc46),('magnitude_ratio_table',0x1dc46,0x1ec46),('perspective_factors',0x5c6b8,0x5c6c4)]
DYNAMIC=[('track_grid',0x1c280,256),('segment_coordinates',0x1c588,100),('segment_count',0x1ca1a,1),
 ('primary_geometry_indices',0x1c4c0,100),('alternate_geometry_indices',0x1c524,100),('segment_properties',0x1c5ec,100),
 ('primary_bases',0x1c650,200),('alternate_bases',0x1c718,200),('geometry_window',0x1ef82,0x10100)]
def sha(b):return hashlib.sha256(b).hexdigest()
def require(x,message):
    if not x:raise ValueError(message)
def canonical(text,symbols=None):
    text=_canonical(text,symbols)
    text=re.sub(r'^(b\w+)\.[ws] ',r'\1 ',text)
    return re.sub(r'^exg\.l ', 'exg ',text)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--capture',type=Path,action='append');args=ap.parse_args()
    adf=(ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes();require(sha(adf)==ADF_SHA,'ADF identity')
    payload=adf[DISK:DISK+SIZE];require(sha(payload)==PAYLOAD_SHA and payload==(REF/'offline-extracted-payload.bin').read_bytes(),'Payload identity')
    p=json.loads((REF/'provenance.json').read_text(encoding='utf-8-sig'));require(p['commit']==COMMIT,'Reference commit')
    info=next(f for f in p['files'] if f['path']=='StuntCarRacer.s');source=(REF/info['path']).read_bytes();require(sha(source)==SOURCE_SHA==info['sha256'],'Reference source hash')
    lines=source.decode().splitlines();symbols={'lbb00d4e5':0x1bbe5}
    for line in lines:
        m=re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)',line,re.I)
        if m:symbols[m[1].lower()]=BASE+int(m[2],16)
        m=re.match(r'^(lbC[0-9a-f]+):',line,re.I)
        if m:symbols[m[1].lower()]=BASE+int(m[1][3:],16)
    symbols.update({n.lower():a for n,a,_,_ in ROUTINES})
    c=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000);routines=[];listing=[];differences=[]
    for name,a,z,next_label in ROUTINES:
        code=payload[a-BASE:z-BASE];decoded=list(c.disasm(code,a));require(sum(i.size for i in decoded)==len(code),name+' extent')
        first=lines.index(name+':');last=lines.index(next_label+':');local=dict(symbols);items=[]
        for index in range(first+1,last):
            raw=lines[index];text=raw.split(';')[0].strip()
            if name=='calculateTrackPosition':
                if raw.startswith(';') and ('ASL.B' in raw or 'lbB00D4E5' in raw):
                    text=raw[1:].split(';')[0].strip();differences.append({'kind':'restore_commented_original','source_line':index+1,'source':text})
                if text.lower()=='add.b\td0,d0':
                    differences.append({'kind':'skip_enhanced_replacement','source_line':index+1,'source':text});continue
            if name=='checkRoadBoundaries' and '#$10*FRAMERATE_MULTIPLIER' in text:
                differences.append({'kind':'restore_original_restart_countdown','source_line':index+1,'source':text,'original':'MOVE.B #$10,restartTimerCountdown'})
                text=text.replace('#$10*FRAMERATE_MULTIPLIER','#$10')
            if not text:continue
            if ':' in text:
                label,text=text.split(':',1);local[label.strip().lower()]=decoded[len(items)].address;text=text.strip()
            if text:items.append((index+1,text))
        require(len(items)==len(decoded),f'{name} count {len(items)} != {len(decoded)}');records=[]
        for (line,text),i in zip(items,decoded):
            original=i.mnemonic+' '+i.op_str;expected=canonical(text,local);actual=canonical(original)
            if expected!=actual:
                allowed=False
                if i.address in (0x5beb2,0x5bf06,0x5c0a2,0x6024e):
                    allowed=expected.replace('113756','113758')==actual
                elif i.address in (0x60264,0x60280,0x602ac):
                    allowed=expected.replace('113754','113756')==actual
                elif i.address==0x60cf2:
                    allowed=expected=='bpl 396524' and actual=='bpl 396518'
                require(allowed,f'Unexpected operand {i.address:#x}: {expected!r} != {actual!r}')
                differences.append({'kind':'source_operand_difference','address':hex(i.address),'source_line':line,'source':text,'original':original})
            records.append({'address':hex(i.address),'bytes':i.bytes.hex(),'original':original,'source_line':line,'source':text})
            listing.append(f'{i.address:08x} {i.bytes.hex():24s} {original}')
        routines.append({'name':name,'runtime_address':hex(a),'end_exclusive':hex(z),'payload_offset':hex(a-BASE),'disk_offset':hex(DISK+a-BASE),
            'size':len(code),'sha256':sha(code),'instruction_count':len(decoded),'operand_wildcards':0,'instructions':records})
    for name,a,z in PREFIX:
        code=payload[a-BASE:z-BASE];decoded=list(c.disasm(code,a));require(sum(i.size for i in decoded)==len(code),name+' extent')
        routines.append({'name':name,'runtime_address':hex(a),'end_exclusive':hex(z),'size':len(code),'payload_offset':hex(a-BASE),'disk_offset':hex(DISK+a-BASE),
            'sha256':sha(code),'instruction_count':len(decoded),'scope':'Untouched branch/call scaffold; local-practice gates constrain network/opponent calls off.',
            'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'original':i.mnemonic+' '+i.op_str} for i in decoded]})
    tables=[{'name':n,'runtime_address':hex(a),'end_exclusive':hex(z),'size':z-a,'sha256':sha(payload[a-BASE:z-BASE]),'payload_offset':hex(a-BASE),'disk_offset':hex(DISK+a-BASE)} for n,a,z in TABLES]
    captures=[]
    for path in args.capture or [ROOT/'analysis/captures/race']:
        m=json.loads((path/'manifest.json').read_text());b=next(b for b in m['banks'] if b['tag']=='CRAM');ram=(path/b['file']).read_bytes()
        require(b['base']=='0x0' and len(ram)==b['size']==0x80000 and sha(ram)==b['sha256'],'Capture RAM identity')
        require(sha(Path(m['source_path']).read_bytes())==m['source_sha256'],'Source savestate identity')
        for r in routines+tables:
            a,z=int(r['runtime_address'],0),int(r['end_exclusive'],0);require(ram[a:z]==payload[a-BASE:z-BASE],'Captured code/table differs '+r['name'])
        captures.append({'directory':str(path.resolve()),'ram_sha256':sha(ram),'savestate_sha256':m['source_sha256'],'code_and_tables_equal_disk':True,
            'segment_count':ram[0x1ca1a],'selected_track':ram[0x1ca22],'network_mode':ram[0x57c3c],
            'dynamic_data':[{'name':n,'runtime_address':hex(a),'size':s,'sha256':sha(ram[a:a+s])} for n,a,s in DYNAMIC]})
    record={'source_adf_sha256':ADF_SHA,'payload_sha256':PAYLOAD_SHA,'payload_base':hex(BASE),'payload_disk_offset':hex(DISK),
      'reference':{'commit':COMMIT,'repository':p['repository'],'source_url':info['url'],'source_sha256':SOURCE_SHA},
      'routines':routines,'tables':tables,'source_differences':differences,'captures':captures,
      'dynamic_data':[{'name':n,'runtime_address':hex(a),'size':s} for n,a,s in DYNAMIC],
      'contract':['Local practice path: networkMode==0 and selectedTrack highbit clear; no opponent update or renderer execution.',
        'Original D1/D2 bits8..15 zero at entry; all other register bits and CCR randomized by oracle.',
        'Track count1..100; grid entries0xff or valid segment; grid and segment coordinates describe valid nearest-cell geometry.',
        'Curve polar magnitudes must be below0x8000; signed-word minimum falls outside valid geometric DIVU domain and native rejects it.',
        'The function exposes all camera/grid/geometry/boundary/progress outputs and shared scratch, including conditional retained values.'],
      'limitations':['Test harness JSRs untouched camera helper then jumps into original processGameFrame prefix; stops at0x64f80 before rendering.',
        'prepareFrameRendering render-buffer clear is excluded; its rendering-only writes are not part of this API.',
        'Full renderer, later ground/edge collision, wheel visual bounce shift, frame timers and outer-loop restart/end gates are separate stages.']}
    OUT.mkdir(parents=True,exist_ok=True);(OUT/'reference-comparison.json').write_text(json.dumps(record,indent=2)+'\n');(OUT/'original-disassembly.asm').write_text('\n'.join(listing)+'\n')
    print(json.dumps({'comparison':str(OUT/'reference-comparison.json'),'routines':len(routines),'code_bytes':sum(r['size'] for r in routines),
       'instructions':sum(r['instruction_count'] for r in routines),'source_differences':len(differences),'captures':len(captures)},indent=2))
if __name__=='__main__':main()
