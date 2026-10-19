"""Pin the original generated road strip, its producer and coordinate scale."""
import argparse
import csv
import json
import re
from pathlib import Path
from capstone import Cs,CS_ARCH_M68K,CS_MODE_BIG_ENDIAN,CS_MODE_M68K_000
from verify_transform_reference import ROOT,REFERENCE,BASE,DISK_OFFSET,PAYLOAD_SIZE,COMMIT,ADF_SHA,PAYLOAD_SHA,SOURCE_SHA,sha,require,canonical

SPANS=[('generateTrackSegmentData',0x65bec,0x65dea,'initializeTrackSegmentBuffers'),
       ('transformCoordinates',0x65ec4,0x65fca,None),
       ('interpolateOpponentTrackPositionFromData',0x5c6c4,0x5c808,'checkLateralBoundsAndBounce')]
def normal(text,symbols=None):
    text=canonical(text,symbols);text=re.sub(r'^(b\w+)\.[ws] ',r'\1 ',text)
    return re.sub(r'^(lea|btst|bset|bclr)\.[bl] ',r'\1 ',text)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--capture',type=Path,default=ROOT/'analysis/captures/race');parser.add_argument('--output',type=Path,default=ROOT/'analysis/track-mesh');args=parser.parse_args()
    adf=(ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes();require(sha(adf)==ADF_SHA,'ADF changed');payload=adf[DISK_OFFSET:DISK_OFFSET+PAYLOAD_SIZE];require(sha(payload)==PAYLOAD_SHA,'Payload changed')
    source=(REFERENCE/'StuntCarRacer.s').read_bytes();require(sha(source)==SOURCE_SHA,'Source changed');require(json.loads((REFERENCE/'provenance.json').read_text())['commit']==COMMIT,'Commit changed');lines=source.decode().splitlines()
    symbols={}
    for filename in ('code-symbols.csv','data-symbols.csv'):
        for row in csv.DictReader((ROOT/'analysis/map'/filename).open()):symbols[row['name'].lower()]=int(row['ram_address'],0)
    symbols.update({name.lower():start for name,start,_,_ in SPANS})
    symbols.update({'initializetracksegmentbuffers':0x65dea,'segmentgeometrydata':0x7abda,'segmentdatapointers':0x7aa1a,
                    'segmentrenderbuffer':0x7ab5a,'segmentdatawritepointer':0x66102,'segmentrenderflags':0x66100,
                    'setuptrackgeometryforframe2':0x65756,'loadtracksegmentconfiguration':0x5fe56,
                    'transformsegmenttoviewspace':0x5ff94,'transformxcoordinatetoperspective':0x6249c,
                    'transformycoordinatetoperspective':0x624f0})
    decoder=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000);routines=[];listing=[]
    for name,start,end,next_name in SPANS:
        code=payload[start-BASE:end-BASE];decoded=list(decoder.disasm(code,start));require(sum(i.size for i in decoded)==len(code),'Incomplete decode '+name)
        first=lines.index(name+':')+1;last=lines.index(next_name+':',first) if next_name else lines.index('updateDamageBarHUD:',first)
        selected=[];labels={}
        for index in range(first,last):
            text=lines[index].split(';')[0].strip()
            if not text:continue
            if ':' in text:
                label,text=text.split(':',1)
                if len(selected)<=len(decoded):labels[label.lower()]=decoded[len(selected)].address if len(selected)<len(decoded) else end
                text=text.strip()
            if text:selected.append((index+1,text))
        if next_name:require(len(selected)==len(decoded),'Source instruction count differs '+name)
        else:selected=selected[:len(decoded)]
        records=[]
        for ins,(number,text) in zip(decoded,selected):
            actual=normal(ins.mnemonic+' '+ins.op_str);expected=normal(text,{**symbols,**labels});require(actual==expected,f'{name}0x{ins.address:x}: {actual} != {expected}')
            records.append({'address':hex(ins.address),'bytes':ins.bytes.hex(),'original':ins.mnemonic+' '+ins.op_str,'source_line':number,'source':text})
        routines.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'payload_offset':hex(start-BASE),'disk_offset':hex(start-BASE+DISK_OFFSET),'size':len(code),'sha256':sha(code),'instruction_count':len(decoded),'operand_wildcards':0,'instructions':records})
        listing.append('; '+name+' SHA256 '+sha(code));listing.extend(f'{i.address:#x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}' for i in decoded)
    manifest=json.loads((args.capture/'manifest.json').read_text());bank=next(b for b in manifest['banks'] if b['tag']=='CRAM');ram=(args.capture/bank['file']).read_bytes();require(len(ram)==0x80000 and int(bank['base'],0)==0 and sha(ram)==bank['sha256'],'RAM mapping/hash');require(sha(Path(manifest['source_path']).read_bytes())==manifest['source_sha256'],'USS changed')
    for _,start,end,_ in SPANS:require(ram[start:end]==payload[start-BASE:end-BASE],'Captured code differs')
    word=lambda a:int.from_bytes(ram[a:a+2],'big');longword=lambda a:int.from_bytes(ram[a:a+4],'big');signed=lambda v:v if v<32768 else v-65536
    start,end=0x7abda,longword(0x66102);count=ram[0x1ca1a];require(0<count<=100 and start<end<=len(ram) and (end-start)%14==0,'Generated cache bounds')
    vertices=[];sections=[]
    for address in range(start,end,14):
        flags,segment=ram[address],ram[address+1];require(segment<count,'Generated segment index');cell=ram[0x1c588+segment];points=[]
        for side in range(2):
            x,z,h=[word(address+2+side*6+c*2) for c in range(3)];point=[(cell&15)*128+signed(x)/16,signed(h)/32,(cell>>4)*128+signed(z)/16];points.append(point);vertices.append(point)
        sections.append({'ram_address':hex(address),'segment':segment,'flags':flags,'points':points})
    pointers=[longword(0x7aa1a+i*4) for i in range(count)]
    for segment,pointer in enumerate(pointers):require(start<=pointer<end and (pointer-start)%14==0 and ram[pointer+1]==segment,'Segment pointer mismatch')
    report={'source_adf_sha256':ADF_SHA,'payload_sha256':PAYLOAD_SHA,'reference_commit':COMMIT,'reference_source_sha256':SOURCE_SHA,'routines':routines,'tables':[],
            'capture':{'directory':str(args.capture.resolve()),'ram_sha256':sha(ram),'source_sha256':manifest['source_sha256']},
            'generated_cache':{'begin':hex(start),'end_exclusive':hex(end),'bytes':end-start,'sha256':sha(ram[start:end]),'segment_count':count,'cross_sections':len(sections),'vertices':len(vertices),'quads':len(sections),'pointer_table':{'address':'0x7aa1a','sha256':sha(ram[0x7aa1a:0x7aa1a+count*4]),'pointers':[hex(p) for p in pointers]},'segment_coordinates':{'address':'0x1c588','sha256':sha(ram[0x1c588:0x1c588+count])}},
            'coordinate_contract':{'x':'gridX*128 + signed(localX)/16','z':'gridZ*128 + signed(localZ)/16','y':'signed(rawHeight)/32','car':'worldPosition /65536','original_visual_projection':'Horizontal localwords ASR1 at0x65f90/92; vertical(cameraHeightBaseline-rawHeight) ASR3 at0x65fb4..65fbc. Therefore visualY is half physicalY relative to horizontal.','grid_cell_world_units':128,'strip_topology':'Pair each cross-section with the next in generated order, final section wraps to first (original0x6607a..0x6608c).','flags':'Header high byte preserved. Flag0x20 marks largeheightstep, changes original surface drawing color; original curved/bank flags0x40/80 preserved.'},
            'native_scope':'Immutable road surface mesh from original generated cache. No reconstructed textures, rails, supports, landscape, cockpit sprites or original rasterizer claimed.',
            'observed_bounds':{'minimum':[min(p[c] for p in vertices) for c in range(3)],'maximum':[max(p[c] for p in vertices) for c in range(3)]},
            'rea_evidence':'analysis/native-render/rea-0x65bec.json','sections':sections}
    args.output.mkdir(parents=True,exist_ok=True);(args.output/'reference-comparison.json').write_text(json.dumps(report,indent=2)+'\n');(args.output/'original-disassembly.asm').write_text('\n'.join(listing)+'\n')
    print(json.dumps({'report':str(args.output/'reference-comparison.json'),'instruction_count':sum(r['instruction_count'] for r in routines),'cross_sections':len(sections),'vertices':len(vertices),'quads':len(sections),'operand_wildcards':0},indent=2))
if __name__=='__main__':main()
