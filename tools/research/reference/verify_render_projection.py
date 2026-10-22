"""Pin original angular road projection, screen rotation and vertex units."""
from pathlib import Path
import argparse
import hashlib
import json
import re
from capstone import Cs,CS_ARCH_M68K,CS_MODE_BIG_ENDIAN,CS_MODE_M68K_000
from verify_track_progress import canonical

ROOT=Path(__file__).resolve().parents[3]
REF=ROOT/'analysis/reference/vesuri'
BASE=0xe700
ADF_SHA='548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA='508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA='321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT='3cce78d193c3d0208be34d0bbfc3155e787cd406'
ROUTINES=[('transformXCoordinateToPerspective',0x6249c,0x624c2,'transformCoordinate'),
    ('transformYCoordinateToPerspective',0x624f0,0x6250a,'apply3DTransformation'),
    ('applyScreenSpaceRotation',0x62518,0x62574,'generateRandomNumber'),
    ('transformCoordinates',0x65ec4,0x6607a,None)]
def sha(data):return hashlib.sha256(data).hexdigest()
def require(value,message):
    if not value:raise ValueError(message)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,default=ROOT/'analysis/captures/race')
    parser.add_argument('--output',type=Path,default=ROOT/'analysis/render-projection')
    args=parser.parse_args()
    adf=(ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes();require(sha(adf)==ADF_SHA,'ADF changed')
    payload=adf[0xdc00:0xdc00+408540];require(sha(payload)==PAYLOAD_SHA,'Payload changed')
    provenance=json.loads((REF/'provenance.json').read_text());require(provenance['commit']==COMMIT,'Reference commit changed')
    info=next(f for f in provenance['files'] if f['path']=='StuntCarRacer.s')
    source=(REF/'StuntCarRacer.s').read_bytes();require(sha(source)==SOURCE_SHA==info['sha256'],'Reference changed')
    lines=source.decode().splitlines();symbols={}
    for line in lines:
        match=re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)',line,re.I)
        if match:symbols[match[1].lower()]=BASE+int(match[2],16)
        match=re.match(r'^(lbC[0-9a-f]+):',line,re.I)
        if match:symbols[match[1].lower()]=BASE+int(match[1][3:],16)
    symbols.update({name.lower():start for name,start,_,_ in ROUTINES})
    symbols.update({'calculatedivision':0x64d66,'calculatevectormagnitude':0x64de8,'transformsegmenttoviewspace':0x5ff94,
        'segmentdatapointers':0x7aa1a,'segmentrenderbuffer':0x7ab5a})
    manifest=json.loads((args.capture/'manifest.json').read_text());bank=next(b for b in manifest['banks'] if b['tag']=='CRAM')
    chip=(args.capture/bank['file']).read_bytes();require(int(bank['base'],0)==0 and len(chip)==bank['size']==0x80000,'RAM mapping')
    require(sha(chip)==bank['sha256'],'RAM changed');require(sha(Path(manifest['source_path']).read_bytes())==manifest['source_sha256'],'USS changed')
    decoder=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000);routines=[];listing=[]
    for name,start,end,next_name in ROUTINES:
        data=payload[start-BASE:end-BASE];require(chip[start:end]==data,name+' captured code differs')
        decoded=list(decoder.disasm(data,start));require(sum(i.size for i in decoded)==len(data),'Incomplete decode '+name)
        first=lines.index(name+':')+1
        if next_name:last=lines.index(next_name+':')
        else:last=next(n for n in range(first,len(lines)) if lines[n].strip()=='MOVE.L\t(SP)+,A6')
        selected=[];local=dict(symbols)
        for index in range(first,last):
            text=lines[index].split(';')[0].strip()
            if not text:continue
            if ':' in text:
                label,text=text.split(':',1);local[label.lower()]=decoded[len(selected)].address if len(selected)<len(decoded) else end;text=text.strip()
            if text:selected.append((index+1,text))
        require(len(selected)==len(decoded),f'{name}: source instruction count {len(selected)} !={len(decoded)}')
        instructions=[]
        for ins,(number,text) in zip(decoded,selected):
            actual=canonical(ins.mnemonic+' '+ins.op_str);expected=canonical(text,local)
            require(actual==expected,f'{name} {ins.address:#x}: {actual} !={expected} ({text})')
            instructions.append({'address':hex(ins.address),'bytes':ins.bytes.hex(),'original':ins.mnemonic+' '+ins.op_str,'source_line':number,'source':text})
        routines.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'payload_offset':hex(start-BASE),
            'disk_offset':hex(start-BASE+0xdc00),'sha256':sha(data),'size':len(data),'capture_equals_original':True,
            'instruction_count':len(decoded),'operand_wildcards':0,'source_operand_differences':[],'instructions':instructions})
        listing.append('; '+name+' SHA256 '+sha(data));listing.extend(f'{i.address:#x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}' for i in decoded);listing.append('')
    supplemental=[]
    for name,start,end in [('camera parameters including grid conversion',0x600a6,0x60246),('screen roll matrix producer',0x61368,0x6158c),
            ('trig and polar lookup routines',0x64d08,0x64e18),('segment grid to view transform',0x5ff94,0x6001c)]:
        data=payload[start-BASE:end-BASE];require(chip[start:end]==data,name+' captured bytes differ')
        supplemental.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'sha256':sha(data),
            'capture_equals_original':True,'source_comparison':'Previously recovered by transform/track-progress verifiers; native projection oracle executes original bytes.'})
    tables=[]
    for name,start,end in [('trig table',0x1ca42,0x1cc46),('polar angle table',0x1cc46,0x1dc46),('polar magnitude table',0x1dc46,0x1ec46)]:
        data=payload[start-BASE:end-BASE];require(chip[start:end]==data,name+' captured table differs')
        tables.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'payload_offset':hex(start-BASE),
            'disk_offset':hex(start-BASE+0xdc00),'sha256':sha(data),'capture_equals_original':True})
    report={'source_adf_sha256':ADF_SHA,'payload_sha256':PAYLOAD_SHA,'reference_commit':COMMIT,'reference_source_sha256':SOURCE_SHA,
        'reference_url':info['url'],'routines':routines,'supplemental_original_routines':supplemental,'tables':tables,
        'capture':{'directory':str(args.capture.resolve()),'ram_sha256':sha(chip),'source_sha256':manifest['source_sha256']},
        'model':'Original angular projection: polar angle minus camera residual yaw, then vertical polar angle minus viewportCenterY, then Q15 screen-space roll.',
        'units':{'simulation':'World high-word units: full positionQ16/65536. Grid cell=128.',
            'mesh':'X/Z localWord/16 +grid*128. Y rawHeight/32.',
            'original_horizontal_input':'localX/localZ ASR1 plus grid offsets;8 projection units per simulation world high-word unit.',
            'original_vertical_input':'(cameraHeightBaseline-rawHeight) ASR3;4 projection units per simulation Y high-word unit.',
            'vertical_scale':'Projection therefore halves simulation-world Y relative to X/Z.',
            'screen':'Viewport-local integer center(128,64); dimensions256x128. Framebuffer viewport offset belongs to caller.',
            'horizontal_angular_span':'Approximately45degrees across256pixels before roll (angleUnits/32 pixels,65536units per turn).',
            'angles':'65536 units/turn; yaw0 looks+Z, positive yaw turns towards+X. Positive pitch moves horizon down. Positive roll rotates image counterclockwise.'},
        'camera_eye':'cameraHeightBaseline=(worldY>>11)+signed(speed-derived bob/pitch term>>4), recovered by updateTrackCamera.',
        'roll_scheduling':'The original screen-rotation pair is read from existing matrix words16/17, produced before integration. The pose-only maker recomputes it; the replay viewer should supply its evolved matrix pair for original scheduling.',
        'rea_evidence':['analysis/native-render/rea-0x6249c.json','analysis/native-render/rea-0x624f0.json','analysis/native-render/rea-0x62518.json'],
        'ghidra_scope':'REA/Ghidra0x62518 body metadata covers only18bytes; full MC68000 span0x62518..0x62574 is byte/source verified and executes in the oracle.',
        'coverage':'Exact projection helper word outputs and complete original first-road-vertex transform tested against native world-unit input. Original renderer visibility queues, clipping and pixel fills are outside this module.',
        'viewer_choices':['Near-plane distance, clipped-vertex interpolation and polygon ordering are explicit viewer choices.',
            'World-point quantization floors to original local coordinate/height word grids; synthetic near-clipped points use the same rule.'],
        'calling_domain':'Absolute signed-word magnitudes must not equal0x8000; original DIVU overflow paths at that boundary are excluded.'}
    args.output.mkdir(parents=True,exist_ok=True);(args.output/'reference-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    (args.output/'original-disassembly.asm').write_text('\n'.join(listing))
    print(json.dumps({'report':str(args.output/'reference-comparison.json'),'instruction_count':sum(r['instruction_count'] for r in routines),'operand_wildcards':0,'source_operand_differences':0},indent=2))
if __name__=='__main__':main()
