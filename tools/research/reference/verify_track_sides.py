"""Pin the original raised-road lower vertices, side topology and materials."""
import argparse
import csv
import json
import re
import subprocess
from pathlib import Path
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_transform_reference import (ROOT, REFERENCE, BASE, DISK_OFFSET, PAYLOAD_SIZE,
    COMMIT, ADF_SHA, PAYLOAD_SHA, SOURCE_SHA, sha, require, canonical)

# Source boundaries are labels, never byte-pattern wildcards. Prefix spans end
# immediately before the following source instruction recorded below.
SPANS = [
    ('transformCoordinates', 0x65ec4, 0x6607a, 'transformCoordinates', 'MOVE.L\t(SP)+,A6'),
    ('nearRightMaterial', 0x68aa0, 0x68ad2, 'lbC05A3A0', 'MOVE.L\tD0,(A1)+'),
    ('nearLeftMaterial', 0x68c36, 0x68c68, 'lbC05A536', 'MOVE.L\tD0,(A1)+'),
    ('roadMaterial', 0x68d82, 0x68df4, 'renderTrackSurface', 'MOVE.L\tD0,(A1)+'),
    ('renderLeftTrackSidePanel', 0x691b4, 0x69270, 'renderLeftTrackSidePanel', 'renderRightTrackSidePanel:'),
    ('renderRightTrackSidePanel', 0x69270, 0x6932c, 'renderRightTrackSidePanel', 'rasterizeVerticalEdge:'),
    ('nearLongitudinalEdgeMaterial', 0x673ee, 0x67438, None, 'MOVE.B\tsegmentProcessedFlag,$02(A1,D3.W)'),
    ('drawTrackLines', 0x67460, 0x6770a, 'drawTrackLines', 'setupBitplaneMasks:'),
]

def normal(text, symbols=None):
    text=canonical(text, symbols)
    text=re.sub(r'^(b\w+)\.[ws] ', r'\1 ', text)
    return re.sub(r'^(lea|btst|bset|bclr)\.[bl] ', r'\1 ', text)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,default=ROOT/'analysis/captures/race')
    parser.add_argument('--output',type=Path,default=ROOT/'analysis/track-sides')
    parser.add_argument('--oracle',type=Path,help='Optional built original-code differential executable')
    args=parser.parse_args()
    adf=(ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes();require(sha(adf)==ADF_SHA,'ADF changed')
    payload=adf[DISK_OFFSET:DISK_OFFSET+PAYLOAD_SIZE];require(sha(payload)==PAYLOAD_SHA,'Payload changed')
    source=(REFERENCE/'StuntCarRacer.s').read_bytes();require(sha(source)==SOURCE_SHA,'Source changed')
    require(json.loads((REFERENCE/'provenance.json').read_text())['commit']==COMMIT,'Reference commit changed')
    lines=source.decode().splitlines();symbols={}
    for line in lines:
        m=re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)',line,re.I)
        if m:symbols[m[1].lower()]=BASE+int(m[2],16)
        m=re.match(r'^(lbC[0-9a-f]+):',line,re.I)
        if m:symbols[m[1].lower()]=BASE+int(m[1][3:],16)
    for filename in ('code-symbols.csv','data-symbols.csv'):
        for row in csv.DictReader((ROOT/'analysis/map'/filename).open()):symbols[row['name'].lower()]=int(row['ram_address'],0)
    symbols.update({name.lower():start for name,start,_,_,_ in SPANS})
    symbols.update({'segmentdatapointers':0x7aa1a,'segmentrenderbuffer':0x7ab5a,'segmentgeometrydata':0x7abda,
        'rendercommandqueue':0x7b08a,'edgeSortBuffer'.lower():0x69b7c,
        'transformsegmenttoviewspace':0x5ff94,'transformxcoordinatetoperspective':0x6249c,
        'transformycoordinatetoperspective':0x624f0,'applyscreenspacerotation':0x62518,'renderquadrilateral':0x66618,
        'drawclippedline':0x668a8,'calculaterenderdepth':0x66ffe})
    manifest=json.loads((args.capture/'manifest.json').read_text());bank=next(b for b in manifest['banks'] if b['tag']=='CRAM')
    ram=(args.capture/bank['file']).read_bytes();require(int(bank['base'],0)==0 and len(ram)==0x80000 and sha(ram)==bank['sha256'],'RAM hash/mapping changed')
    require(sha(Path(manifest['source_path']).read_bytes())==manifest['source_sha256'],'USS changed')
    decoder=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000);routines=[];listing=[]
    for name,start,end,label,stop in SPANS:
        code=payload[start-BASE:end-BASE];require(ram[start:end]==code,name+' captured code differs')
        decoded=list(decoder.disasm(code,start));require(sum(i.size for i in decoded)==len(code),'Incomplete decode '+name)
        if label:first=lines.index(label+':')+1
        else:
            # This instruction belongs to generateTrackEdgeLines, after the
            # queue segment/steering fields, before depth-color selection.
            first=next(i for i in range(lines.index('lbC058CD8:')+1,len(lines)) if lines[i].strip()=='MOVE.B\t#$09,D0')
        last=next(i for i in range(first,len(lines)) if lines[i].strip()==stop)
        selected=[];local=dict(symbols)
        for number in range(first,last):
            text=lines[number].split(';')[0].strip()
            if not text:continue
            if ':' in text:
                tag,text=text.split(':',1);local[tag.lower()]=decoded[len(selected)].address if len(selected)<len(decoded) else end;text=text.strip()
            if text:selected.append((number+1,text))
        require(len(selected)==len(decoded),f'{name}: instruction count {len(selected)} != {len(decoded)}')
        records=[]
        for ins,(number,text) in zip(decoded,selected):
            actual=normal(ins.mnemonic+' '+ins.op_str);expected=normal(text,local)
            require(actual==expected,f'{name} {ins.address:#x}: {actual} != {expected} ({text})')
            records.append({'address':hex(ins.address),'bytes':ins.bytes.hex(),'source_line':number,'source':text,'original':ins.mnemonic+' '+ins.op_str})
        routines.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'payload_offset':hex(start-BASE),
            'disk_offset':hex(start-BASE+DISK_OFFSET),'size':len(code),'sha256':sha(code),'capture_equals_original':True,
            'instruction_count':len(decoded),'operand_wildcards':0,'instructions':records})
        listing.append('; '+name+' SHA256 '+sha(code));listing.extend(f'{i.address:#x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}' for i in decoded)
    report={'source_adf_sha256':ADF_SHA,'payload_sha256':PAYLOAD_SHA,'reference_commit':COMMIT,'reference_source_sha256':SOURCE_SHA,
        'routines':routines,'capture':{'directory':str(args.capture.resolve()),'ram_sha256':sha(ram),'source_sha256':manifest['source_sha256']},
        'geometry':{'description':'Raised-road side panels, not guardrails. Same X/Z as upper road edge; lower raw height512 = worldY16.',
            'ground_raw_height':512,'ground_world_y':16,'lower_vertex_lookup_offset':'0x78',
            'right_lower_vertex_required':'signed(currentAngularX)<signed(previousAngularX), or equal with offRoadSideFlags!=0, or flags&0x80',
            'left_lower_vertex_required':'signed(currentAngularX)>signed(previousAngularX), or equal with offRoadSideFlags!=0, or flags&0x40',
            'right_far_edge_offsets':[8,16,0,-16],'left_far_edge_offsets':[12,-12,4,20]},
        'materials':{'road':'flags&0x20 =>0; otherwise segment&1 =>2, even=>1',
            'far_side':'segment&1 =>trackColorIndex2; even=>15','near_side':'Same as far unless preview segmentRenderingFlag!=0 =>trackColorIndex1',
            'side_vertical_edges':'trackColorIndex1','surface_cross_edge':'15 only if segmentPropertyFlags&1',
            'near_road_longitudinal_edges':'signed segmentDepthCounter<38 =>9, <42 =>10, else11. At depth>=44 set0x80 to suppress; at lesser depths alternate flag changes color to3.',
            'near_alternate_flag':'((segmentAlternateFlag<<1)^crossSectionByteOffset)&4',
            'capture_track_color_index1':ram[0x691b3],'capture_track_color_index2':ram[0x691b2],
            'parity_contract':'Far queue+0x1c contains generated header lowbyte (segment), while +0x1e contains highbyte (flags).'},
        'strip_metadata':'drawTrackLines constructs previousD2=>currentD1 then reads header at currentD1. Native quad i=>next therefore uses the ending section header, including the cyclic final quad.',
        'preview_override_scope':'Only source writers of segmentRenderingFlag are renderTrackPreviewGrid; race side models leave this explicit override false.',
        'native_scope':'Exact immutable side geometry/materials plus lower-vertex admission helper. Native depth clipping, surface visibility, triangulation and near/far queue equivalence are not claimed.',
        'rea_evidence':['analysis/track-sides/rea-producer.json','analysis/track-sides/rea-left-panel.json','analysis/track-sides/rea-right-panel.json']}
    args.output.mkdir(parents=True,exist_ok=True);(args.output/'reference-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    (args.output/'original-disassembly.asm').write_text('\n'.join(listing)+'\n')
    if args.oracle:
        oracle_payload=ROOT/'analysis/reference/vesuri/offline-extracted-payload.bin'
        require(sha(oracle_payload.read_bytes())==PAYLOAD_SHA,'Original-code oracle payload changed')
        result=subprocess.run([str(args.oracle.resolve()),str(oracle_payload),str((args.capture/bank['file']).resolve())],capture_output=True,text=True,check=True)
        oracle=json.loads(result.stdout);require(oracle['mismatches']==0 and oracle['original_code_unmodified'] is True,'Oracle failed')
        require(oracle['total_cases']>=9000 and oracle['admission_cases']>=6000 and oracle['material_cases']>=3000,'Insufficient oracle cases')
        oracle['executable']={'path':str(args.oracle.resolve()),'sha256':sha(args.oracle.read_bytes())}
        oracle['sources']=[{'path':str((ROOT/name).resolve()),'sha256':sha((ROOT/name).read_bytes())} for name in
            ('src/render/track_sides.h','src/render/track_sides.cpp','src/render/track_mesh.h','src/render/track_mesh.cpp',
             'src/state/track_mesh_adapter.h','tests/support/track_mesh_fixture.h','tests/support/track_sides_fixture.h',
             'tests/oracle/track_sides_oracle.cpp','tools/research/reference/verify_track_sides.py',
             *[path.relative_to(ROOT).as_posix() for path in sorted((ROOT/'src/state').glob('*.h'))])]
        oracle['reference_comparison_sha256']=sha((args.output/'reference-comparison.json').read_bytes())
        (args.output/'oracle.json').write_text(json.dumps(oracle,indent=2)+'\n')
    print(json.dumps({'report':str(args.output/'reference-comparison.json'),'instruction_count':sum(r['instruction_count'] for r in routines),'operand_wildcards':0},indent=2))
if __name__=='__main__':main()
