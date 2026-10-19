"""Verify original near-track queue, painter order and clipping decisions."""
import argparse
import csv
import json
import re
import subprocess
from pathlib import Path
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_transform_reference import (ROOT, REFERENCE, BASE, DISK_OFFSET, PAYLOAD_SIZE,
    COMMIT, ADF_SHA, PAYLOAD_SHA, SOURCE_SHA, sha, require, canonical)

SPANS=[
    ('adjustCoordinateBoundsFromCenter',0x617d0,0x6180e,'applyMomentumAmplification'),
    ('processTrackVisibility',0x655ea,0x65720,'processTrackVertexWithCharacteristics'),
    ('processTrackVertexWithCharacteristics',0x65720,0x65756,'setupTrackGeometryForFrame2'),
    ('processTrackSegments',0x6702e,0x67092,'generateTrackEdgeLines'),
    ('generateTrackEdgeLines',0x67092,0x67460,'drawTrackLines'),
    ('clipLineAndMakeEdge',0x6814a,0x681ba,'extractVertexCoordinates'),
    ('extractVertexCoordinates',0x681ba,0x681d4,'checkEdgeClippingRequired'),
    ('checkEdgeClippingRequired',0x681d6,0x6847c,'checkFarClippingPlane'),
    ('renderTrackSurfaceEdge',0x688fc,0x6892a,'renderRightBarrierEdges'),
    ('renderRightBarrierEdges',0x6892a,0x6897c,'renderLeftBarrierEdges'),
    ('renderLeftBarrierEdges',0x6897c,0x689ce,'renderLeftRoadEdge'),
    ('renderLeftRoadEdge',0x689ce,0x68a12,'renderRightRoadEdge'),
    ('renderRightRoadEdge',0x68a12,0x68a56,'renderRightBarrier'),
    ('renderRightBarrier',0x68a56,0x68bec,'renderLeftBarrier'),
    ('renderLeftBarrier',0x68bec,0x68d82,'renderTrackSurface'),
    ('renderTrackSurface',0x68d82,0x68f28,'renderTrackNear'),
    ('renderTrackNear',0x68f28,0x690bc,'renderTrackFar'),
]

def normal(text,symbols=None):
    if symbols:
        text=re.sub(r'(?<!\w)\.[a-zA-Z_]\w*',lambda m:str(symbols[m[0].lower()]),text)
        parts=text.lower().split(None,1)
        def resolve(match):
            token=match[0]
            if token in symbols:return str(symbols[token])
            if re.fullmatch(r'[ad][0-7]|sp|ccr|w|l',token):return token
            raise ValueError('Unresolved source symbol: '+token)
        operand=re.sub(r'(?<![\w$])[a-z_][\w]*',resolve,parts[1] if len(parts)>1 else '')
        text=parts[0]+' '+operand
    text=canonical(text);text=re.sub(r'^(b\w+)\.[ws] ',r'\1 ',text)
    return re.sub(r'^(lea|btst|bset|bclr|bchg|exg)\.[bl] ',r'\1 ',text)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--capture',type=Path,default=ROOT/'analysis/captures/race')
    parser.add_argument('--output',type=Path,default=ROOT/'analysis/near-track-visibility');parser.add_argument('--oracle',type=Path)
    parser.add_argument('--inputs',type=Path,default=ROOT/'tests/replays/laps/authored-race-1463.csv');args=parser.parse_args()
    adf=(ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes();require(sha(adf)==ADF_SHA,'ADF changed');payload=adf[DISK_OFFSET:DISK_OFFSET+PAYLOAD_SIZE];require(sha(payload)==PAYLOAD_SHA,'Payload changed')
    source=(REFERENCE/'StuntCarRacer.s').read_bytes();require(sha(source)==SOURCE_SHA,'Source changed');require(json.loads((REFERENCE/'provenance.json').read_text())['commit']==COMMIT,'Reference commit changed')
    lines=source.decode().splitlines();symbols={}
    for line in lines:
        m=re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)',line,re.I)
        if m:symbols[m[1].lower()]=BASE+int(m[2],16)
        m=re.match(r'^(lbC[0-9a-f]+):',line,re.I)
        if m:symbols[m[1].lower()]=BASE+int(m[1][3:],16)
    for filename in ('code-symbols.csv','data-symbols.csv'):
        for row in csv.DictReader((ROOT/'analysis/map'/filename).open()):symbols[row['name'].lower()]=int(row['ram_address'],0)
    symbols.update({name.lower():start for name,start,_,_ in SPANS})
    symbols.update({'segmentdatapointers':0x7aa1a,'segmentrenderbuffer':0x7ab5a,'segmentgeometrydata':0x7abda,'rendercommandqueue':0x7b08a,
        'edgesortbuffer':0x69b7c,'linedrawingbufferend':0x60a8,'drawclippedline':0x668a8,'processTrackCharacteristics'.lower():0x5a5b6,
        'loadandtransformtrackvertex':0x5a58e,'manageRenderBounds'.lower():0x66ff0,'drawtracksegmentwireframe':0x672b6,
        'setpixelcolor':0x66348,'rasterizeverticaledge':0x6932e,'initializeedgesortbuffers':0x68544,
        'renderopponentcarifahead':0x6916c,'renderopponentcarifbehind':0x6917c,'renderopponentcarifoccluded':0x69188})
    manifest=json.loads((args.capture/'manifest.json').read_text());bank=next(b for b in manifest['banks'] if b['tag']=='CRAM');ram=(args.capture/bank['file']).read_bytes()
    require(int(bank['base'],0)==0 and len(ram)==0x80000 and sha(ram)==bank['sha256'],'RAM hash/mapping changed');require(sha(Path(manifest['source_path']).read_bytes())==manifest['source_sha256'],'USS changed')
    decoder=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000);routines=[];listing=[];differences=[]
    for name,start,end,next_name in SPANS:
        code=payload[start-BASE:end-BASE];require(ram[start:end]==code,name+' capture differs');decoded=list(decoder.disasm(code,start));require(sum(i.size for i in decoded)==len(code),'Incomplete decode '+name)
        first=lines.index(name+':')+1;last=lines.index(next_name+':',first);selected=[];local=dict(symbols)
        for number in range(first,last):
            text=lines[number].split(';')[0].strip()
            if not text:continue
            if ':' in text:
                tag,text=text.split(':',1);local[tag.lower()]=decoded[len(selected)].address if len(selected)<len(decoded) else end;text=text.strip()
            if text:selected.append((number+1,text))
        require(len(selected)==len(decoded),f'{name}: source instruction count {len(selected)} !={len(decoded)}');records=[]
        for ins,(number,text) in zip(decoded,selected):
            actual=normal(ins.mnemonic+' '+ins.op_str);expected=normal(text,local)
            # Original disk's lateral-offset word follows the additional word
            # retained by our physics fixtures. The enhanced equate is two bytes
            # earlier; record the concrete original operand, never wildcard it.
            difference=None
            if actual!=expected:
                corrected=normal(text,{**local,'offsetfromroadcenter':0x1bc5c})
                if actual==corrected:difference={'source_line':number,'source':text,'original':ins.mnemonic+' '+ins.op_str,'reason':'Original lateral offset word0x1bc5c; enhanced equate0x1bc5a.'}
                else:raise ValueError(f'{name} {ins.address:#x}: {actual} !={expected} ({text})')
            if difference:differences.append(difference)
            records.append({'address':hex(ins.address),'bytes':ins.bytes.hex(),'source_line':number,'source':text,'original':ins.mnemonic+' '+ins.op_str})
        routines.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'payload_offset':hex(start-BASE),'disk_offset':hex(start-BASE+DISK_OFFSET),
            'size':len(code),'sha256':sha(code),'instruction_count':len(decoded),'operand_wildcards':0,'instructions':records})
        listing.append('; '+name+' SHA256 '+sha(code));listing.extend(f'{i.address:#x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}' for i in decoded)
    report={'source_adf_sha256':ADF_SHA,'payload_sha256':PAYLOAD_SHA,'reference_commit':COMMIT,'reference_source_sha256':SOURCE_SHA,'routines':routines,'source_operand_differences':differences,
        'capture':{'directory':str(args.capture.resolve()),'ram_sha256':sha(ram),'source_sha256':manifest['source_sha256']},
        'geometry_scope':'Dense near geometry and forced lower projection are separately verified by near_track_geometry; this module consumes typed screen vertices.',
        'queue':'Original7edges plus segment/depth color/processed/steering header; previous/current pairing, sparse flags and processed-side patches are retained.',
        'line_buffer':{'original_begin':'0x3d80','original_end':'0x60a8','capacity_bytes':9000,'enhanced_source_declared_bytes':10000},
        'line_clipping':'Original near mode0 retains pre-Xclip Yheader and pads boundary rows; clipped endpoint metadata and storage are checked against original drawClippedLine.',
        'painter':'Original right/left/surface merge rules, visibility groups, lateral draw order, conditional white cross edges, yellow/brown longitudinal edges and viewport closure.',
        'boundary_filling':'Native raster coverage consumes original ordered edge boundaries; original blitter/scanline pixel equality is not claimed.',
        'rea_evidence':['analysis/near-track/rea-visibility.json','analysis/near-track/rea-segments.json','analysis/near-track/rea-edge-queue.json','analysis/near-track/rea-0x68f28.json','analysis/near-track/rea-closure.json']}
    args.output.mkdir(parents=True,exist_ok=True);(args.output/'reference-comparison.json').write_text(json.dumps(report,indent=2)+'\n');(args.output/'original-disassembly.asm').write_text('\n'.join(listing)+'\n')
    if args.oracle:
        original=ROOT/'analysis/reference/vesuri/offline-extracted-payload.bin';require(sha(original.read_bytes())==PAYLOAD_SHA,'Oracle payload changed')
        controls=args.inputs.resolve();rows=list(csv.DictReader(controls.open(newline='')));require(rows,'No replay controls')
        for tick,row in enumerate(rows):require(int(row['tick'])==tick and 0<=int(row['controls'])<=31 and int(row.get('quit',0)) in (0,1),'Invalid replay controls')
        provenance=controls.with_suffix('.provenance.json');provenance_identity=None
        if provenance.exists():
            p=json.loads(provenance.read_text());require(p['input_sha256']==sha(controls.read_bytes()) and p['ticks']==len(rows),'Controls provenance differs')
            require(p['starting_capture']['chip_ram_sha256']==bank['sha256'],'Controls start from another capture')
            provenance_identity={'path':str(provenance),'sha256':sha(provenance.read_bytes())}
        command=[str(args.oracle.resolve()),str(original),str((args.capture/bank['file']).resolve()),str(controls)]
        result=subprocess.run(command,capture_output=True,text=True)
        require(result.returncode==0,'Oracle failed: '+result.stderr);oracle=json.loads(result.stdout);require(oracle['mismatches']==0 and oracle['original_code_unmodified'] is True,'Oracle result invalid')
        require(oracle['full_near_frames']==len(rows) and oracle['full_near_segment_passes']==3*len(rows) and oracle['full_frame_projection_code_executed'] is True,'Full near replay was not checked')
        oracle['command']=command;oracle['controls']={'path':str(controls),'sha256':sha(controls.read_bytes()),'ticks':len(rows)};oracle['controls_provenance']=provenance_identity
        oracle['executable']={'path':str(args.oracle.resolve()),'sha256':sha(args.oracle.read_bytes())}
        dependencies=set((ROOT/'src').glob('*.h'))|set((ROOT/'src').glob('*.cpp'))|set((ROOT/'tests').glob('*fixture.h'))
        dependencies.update({ROOT/'tests/driving_replay_io.h',ROOT/'tests/near_track_visibility_oracle.cpp',Path(__file__),ROOT/'CMakeLists.txt'})
        oracle['sources']=[{'path':str(n.resolve()),'sha256':sha(n.read_bytes())} for n in sorted(dependencies)]
        oracle['reference_comparison_sha256']=sha((args.output/'reference-comparison.json').read_bytes());(args.output/'oracle.json').write_text(json.dumps(oracle,indent=2)+'\n')
        (args.output/'oracle-stdout.json').write_text(result.stdout);(args.output/'oracle-stderr.txt').write_text(result.stderr)
    print(json.dumps({'report':str(args.output/'reference-comparison.json'),'instruction_count':sum(r['instruction_count'] for r in routines),'operand_wildcards':0,'source_operand_differences':len(differences)},indent=2))
if __name__=='__main__':main()
