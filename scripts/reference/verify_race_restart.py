"""Verify original restart stages and exact enhanced-source differences."""
import argparse
import csv
import json
import re
from pathlib import Path
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
from verify_transform_reference import ROOT, REFERENCE, BASE, DISK_OFFSET, PAYLOAD_SIZE, COMMIT, ADF_SHA, PAYLOAD_SHA, SOURCE_SHA, sha, require, canonical

SPECS=[('initializePlayerAtSegment',0x605b6,0x6076c,'calculatePlayerDistance'),
       ('applyCameraLateralOffset',0x5b506,0x5b56c,'updateEngineAudioPitch'),
       ('initializeGameLoop',0x5daac,0x5dabe,'swapDisplayBuffers'),
       ('initializeDebrisParticlePositions',0x60cde,0x60cf8,None),
       ('retreatToPreviousSegment',0x5c538,0x5c554,'calculateBilinearTrackInterpolation')]

def normal(text,symbols=None):
    text=canonical(text,symbols)
    text=re.sub(r'^(b\w+)\.[ws] ',r'\1 ',text)
    text=re.sub(r'^(lea|btst|bset|bclr)\.[bl] ',r'\1 ',text)
    text=re.sub(r'(\d+)/(\d+)',lambda m:str(int(m[1])//int(m[2])),text)
    return text

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',action='append',type=Path)
    parser.add_argument('--output',type=Path,default=ROOT/'analysis/race-restart')
    args=parser.parse_args()
    adf=(ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes();require(sha(adf)==ADF_SHA,'ADF changed')
    payload=adf[DISK_OFFSET:DISK_OFFSET+PAYLOAD_SIZE];require(sha(payload)==PAYLOAD_SHA,'Payload changed')
    source=(REFERENCE/'StuntCarRacer.s').read_bytes();require(sha(source)==SOURCE_SHA,'Reference changed')
    require(json.loads((REFERENCE/'provenance.json').read_text())['commit']==COMMIT,'Reference commit changed')
    symbols={}
    for filename in ('data-symbols.csv','code-symbols.csv'):
        for row in csv.DictReader((ROOT/'analysis/map'/filename).open()):symbols[row['name'].lower()]=int(row['ram_address'],0)
    symbols.update({name.lower():start for name,start,_,_ in SPECS})
    symbols.update({'loadtracksegmentconfiguration':0x5fe56,'calculatecameratransform':0x60190,'applytracksegmentgeometry':0x5be44,
                    'checkroadboundaries':0x60246,'updategamephysics':0x6185c,'playSample'.lower():0xf362,'updateengineaudiopitch':0x5b56c,'framerate_multiplier':6})
    symbols['lbc051eb0']=0x605b0
    lines=source.decode().splitlines();decoder=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000)
    routines=[];listing=[]
    for name,start,end,next_name in SPECS:
        code=payload[start-BASE:end-BASE];decoded=list(decoder.disasm(code,start));require(sum(i.size for i in decoded)==len(code),'Decode boundary '+name)
        instructions=[{'address':hex(i.address),'bytes':i.bytes.hex(),'original':i.mnemonic+' '+i.op_str} for i in decoded]
        differences=[]
        if next_name:
            first=lines.index(name+':')+1;last=lines.index(next_name+':',first);selected=[];labels={}
            for index in range(first,last):
                text=lines[index].split(';')[0].strip()
                if not text:continue
                if ':' in text:
                    label,text=text.split(':',1);labels[label.lower()]=decoded[len(selected)].address if len(selected)<len(decoded) else end;text=text.strip()
                if text:selected.append((index+1,text))
            require(len(selected)==len(decoded),name+' instruction count differs')
            for ins,(number,text),item in zip(decoded,selected,instructions):
                actual=normal(ins.mnemonic+' '+ins.op_str);expected=normal(text,{**symbols,**labels})
                if ins.address==0x60710:
                    require(actual=='move.w d3,113760' and expected=='move.w d3,113758','Track surface destination difference changed')
                    differences.append({'address':hex(ins.address),'original':actual,'enhanced':expected,'reason':'Original track-surface-height destination0x1bc60; enhanced equate names0x1bc5e.'})
                elif ins.address==0x60762:
                    require(actual=='move.b #8,113641' and expected=='move.b #1,113641','Chain velocity difference changed')
                    differences.append({'address':hex(ins.address),'original':actual,'enhanced':expected,'reason':'Enhanced reference divides initial chain velocity by six; original is8.'})
                else:require(actual==expected,f'{name}0x{ins.address:x}: {actual} != {expected}')
                item.update({'source_line':number,'source':text})
        else:
            differences.append({'reason':'Original directly decoded; enhanced debris loop avoids reloading A1. Retreat helper unchanged but included as dependency.'})
        routines.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'payload_offset':hex(start-BASE),'disk_offset':hex(start-BASE+DISK_OFFSET),
                         'size':len(code),'sha256':sha(code),'instruction_count':len(decoded),'instructions':instructions,'source_differences':differences,'operand_wildcards':0})
        listing.append('; '+name+' SHA256 '+sha(code));listing.extend(f'{i.address:#x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}' for i in decoded)
    captures=[]
    for directory in args.capture or [ROOT/'analysis/captures/race']:
        manifest=json.loads((directory/'manifest.json').read_text());bank=next(b for b in manifest['banks'] if b['tag']=='CRAM');chip=(directory/bank['file']).read_bytes()
        require(sha(chip)==bank['sha256'] and int(bank['base'],0)==0 and len(chip)==0x80000,'Capture mapping/hash')
        require(sha(Path(manifest['source_path']).read_bytes())==manifest['source_sha256'],'USS changed')
        for _,start,end,_ in SPECS:require(chip[start:end]==payload[start-BASE:end-BASE],'Captured restart bytes differ')
        captures.append({'directory':str(directory.resolve()),'ram_sha256':sha(chip),'source_sha256':manifest['source_sha256']})
    tables=[]
    for name,start,end in [('restart geometry flags',0x1f0c2,0x1f0d2)]:
        code=payload[start-BASE:end-BASE];tables.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'payload_offset':hex(start-BASE),'disk_offset':hex(start-BASE+DISK_OFFSET),'sha256':sha(code)})
    report={'source_adf_sha256':ADF_SHA,'payload_sha256':PAYLOAD_SHA,'reference_commit':COMMIT,'reference_source_sha256':SOURCE_SHA,'routines':routines,'tables':tables,'captures':captures,
            'scope':'Full local original initializePlayerAtSegment closure through existing typed geometry/physics; restart clears73 vehicle words but preserves race/lap state.',
            'limitations':['No network restart.','A loaded valid track is required. Native bounded search rejects a track with no restartable segment; original would loop.','Original chain speed8 retained. No host interrupt, rendering or audio waveform equivalence claimed.'],
            'original_stages':{'selection_and_clear':'0x605b6..0x606bc','camera':'0x60190','geometry':'0x5be44','boundaries':'0x60246','physics':'0x6185c','placement_finish':'0x606d4..0x60754','chain':'0x6075a..0x6076c'},
            'rea_evidence':'analysis/race-transitions/rea-0x605b6.json'}
    args.output.mkdir(parents=True,exist_ok=True);(args.output/'reference-comparison.json').write_text(json.dumps(report,indent=2)+'\n');(args.output/'original-disassembly.asm').write_text('\n'.join(listing)+'\n')
    print(json.dumps({'report':str(args.output/'reference-comparison.json'),'instruction_count':sum(r['instruction_count'] for r in routines),'operand_wildcards':0},indent=2))
if __name__=='__main__':main()
