"""Pin original selector, geometry and transition helpers; audit source differences."""
from pathlib import Path
import argparse
import hashlib
import json
import re
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

ROOT=Path(__file__).resolve().parents[2]
REF=ROOT/'analysis/reference/vesuri'
OUT=ROOT/'analysis/track-sampling'
BASE,DISK,SIZE=0xe700,0xdc00,408540
ADF_SHA='548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA='508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA='321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT='3cce78d193c3d0208be34d0bbfc3155e787cd406'
SPANS=[('getTrackDataPoint',0x5c0aa,0x5c138,'calculateTrackPosition'),
       ('updateWheelHeightsFromTrack',0x5c1d0,0x5c3da,'applyTrackDirectionTransform'),
       ('handleSegmentTransition',0x5c484,0x5c51a,'advanceToNextSegment'),
       ('advanceToNextSegment',0x5c51a,0x5c538,'retreatToPreviousSegment'),
       ('retreatToPreviousSegment',0x5c538,0x5c554,'calculateBilinearTrackInterpolation'),
       ('loadTrackSegmentConfiguration',0x5fe56,0x5ff94,'transformSegmentToViewSpace')]
DYNAMIC=[('primary_geometry_indices',0x1c4c0,100),('alternate_geometry_indices',0x1c524,100),
         ('segment_properties',0x1c5ec,100),('primary_interpolation_bases',0x1c650,200),
         ('alternate_interpolation_bases',0x1c718,200),('segment_count',0x1ca1a,1),
         ('encoded_geometry_addressable_window',0x1ef82,0x10100)]
DIFFERENCES={0x5c22e:('add.w 113756,d0','add.w 113758,d0'),
             0x5c280:('move.b #255,d0','move.b 255,d0')}

def sha(data): return hashlib.sha256(data).hexdigest()
def require(condition,message):
    if not condition: raise ValueError(message)

def canonical(text,symbols=None):
    p=text.lower().split(None,1); mnemonic=p[0]; operand=p[1] if len(p)>1 else ''
    operand=re.sub(r'\$([0-9a-f]+)',lambda m:str(int(m[1],16)),operand)
    if symbols is not None:
        def resolve(m):
            t=m[0]
            if t in symbols:return str(symbols[t])
            if re.fullmatch(r'[ad][0-7]|sp|\.?[wl]|ccr',t):return t
            raise ValueError('Unresolved symbol '+t)
        operand=re.sub(r'(?<![\w])\.?[a-z_]\w*',resolve,operand)
    operand=re.sub(r'\ba7\b','sp',operand)
    operand=re.sub(r'(?<![\w])(\d+)\.[wl]\b',r'\1',operand)
    operand=re.sub(r'(?<![\w])0\(', '(',operand)
    mnemonic=re.sub(r'^(move|add|sub|cmp)a\.',r'\1.',mnemonic)
    mnemonic=re.sub(r'^(add|sub|and|or|eor|cmp)i\.',r'\1.',mnemonic)
    mnemonic=re.sub(r'^(muls|mulu|divs|divu)\.w$',r'\1',mnemonic)
    mnemonic=re.sub(r'^(bra|bne|bpl|blt|ble|beq|bcs|bmi)\.[ws]$',r'\1',mnemonic)
    mnemonic=re.sub(r'^(bclr|bset|btst)\.b$',r'\1',mnemonic)
    return mnemonic+' '+re.sub(r'\s+','',operand)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--capture',type=Path,action='append');args=ap.parse_args()
    adf=(ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes();require(sha(adf)==ADF_SHA,'ADF identity')
    payload=adf[DISK:DISK+SIZE];require(sha(payload)==PAYLOAD_SHA,'Payload identity')
    require((REF/'offline-extracted-payload.bin').read_bytes()==payload,'Retained payload differs from original disk range')
    provenance=json.loads((REF/'provenance.json').read_text(encoding='utf-8-sig'))
    require(provenance['commit']==COMMIT,'Pinned reference commit')
    source_info=next(f for f in provenance['files'] if f['path']=='StuntCarRacer.s')
    source=(REF/'StuntCarRacer.s').read_bytes();require(sha(source)==SOURCE_SHA==source_info['sha256'],'Pinned source identity')
    lines=source.decode().splitlines();symbols={}
    for line in lines:
        m=re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)',line,re.I)
        if m:symbols[m[1].lower()]=BASE+int(m[2],16)
        m=re.match(r'^(lbC[0-9a-f]+):',line,re.I)
        if m:symbols[m[1].lower()]=BASE+int(m[1][3:],16)
    symbols.update({name.lower():start for name,start,_,_ in SPANS})
    symbols.update(calculatebilineartrackinterpolation=0x5c554,updatecarworldpositioncoordinate=0x5c5f2)
    decoder=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000)
    routines=[]; found={}; listing=[]
    for name,start,end,next_label in SPANS:
        code=payload[start-BASE:end-BASE]; ins=list(decoder.disasm(code,start))
        require(sum(i.size for i in ins)==len(code) and ins[-1].mnemonic=='rts',name+' code extent')
        first=lines.index(name+':');last=lines.index(next_label+':');source_ins=[];local=dict(symbols)
        for lineno in range(first+1,last):
            t=lines[lineno].split(';')[0].strip()
            if not t:continue
            if t.endswith(':'):
                require(len(source_ins)<len(ins),'Source label exceeds original span')
                local[t[:-1].lower()]=ins[len(source_ins)].address
            else:source_ins.append((lineno+1,t))
        require(len(source_ins)==len(ins),name+' instruction count')
        records=[]
        for (line,t),i in zip(source_ins,ins):
            original=i.mnemonic+' '+i.op_str;expected=canonical(t,local);actual=canonical(original)
            record={'address':hex(i.address),'bytes':i.bytes.hex(),'original':original,'source_line':line,'source':t}
            if expected!=actual:
                require(DIFFERENCES.get(i.address)==(expected,actual),f'Unexpected difference {i.address:#x}: {expected!r} != {actual!r}')
                found[i.address]=(expected,actual);record['modified_source_operand_difference']=True
            records.append(record);listing.append(f'{i.address:08x} {i.bytes.hex():24s} {original}')
        routines.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),
                         'payload_offset':hex(start-BASE),'disk_offset':hex(DISK+start-BASE),'size':len(code),
                         'sha256':sha(code),'instruction_count':len(ins),'source_line':first+1,
                         'operand_wildcards':0,'original_source_operand_differences':sum('modified_source_operand_difference' in r for r in records),
                         'instructions':records})
    require(found==DIFFERENCES,'Expected differences not all observed')
    captures=[]
    for path in args.capture or [ROOT/'analysis/captures/race']:
        m=json.loads((path/'manifest.json').read_text());b=next(b for b in m['banks'] if b['tag']=='CRAM');chip=(path/b['file']).read_bytes()
        require(b['base']=='0x0' and len(chip)==b['size']==0x80000 and sha(chip)==b['sha256'],'Capture RAM identity')
        require(sha(Path(m['source_path']).read_bytes())==m['source_sha256'],'Savestate identity')
        for name,start,end,_ in SPANS:require(chip[start:end]==payload[start-BASE:end-BASE],name+' capture code differs')
        count=chip[0x1ca1a];index=chip[0x1bb1c]
        captures.append({'directory':str(path.resolve()),'ram_sha256':sha(chip),'savestate_sha256':m['source_sha256'],
                         'code_bytes_equal_disk':True,'segment_count':count,'player_segment':index,
                         'selector_entry_inputs_valid':0<count<=100 and index<count,
                         'dynamic_data':[{'name':n,'runtime_address':hex(a),'size':s,'sha256':sha(chip[a:a+s]),
                                          'equals_payload':chip[a:a+s]==payload[a-BASE:a-BASE+s]} for n,a,s in DYNAMIC]})
    record={'source_adf_sha256':ADF_SHA,'payload_sha256':PAYLOAD_SHA,'payload_base':hex(BASE),'payload_disk_offset':hex(DISK),
            'reference':{'repository':provenance['repository'],'commit':COMMIT,'source_sha256':SOURCE_SHA,'source_url':source_info['url']},
            'routines':routines,'tables':[],'dynamic_data':[{'name':n,'runtime_address':hex(a),'size':s} for n,a,s in DYNAMIC],
            'captures':captures,'hardware_accesses':[],
            'compatibility_differences':[{'address':hex(a),'reference':s,'original':o} for a,(s,o) in found.items()],
            'contract':['D1/D2 bits 8..15 must be zero on original entry; upper 16 bits and all other registers may vary.',
                        'segmentCount must be 1..100 and playerSegment below segmentCount; invalid native input throws.',
                        'Geometry offset words are byte-swapped, subtract 0xb100 modulo16, relative to 0x1ef82.',
                        'Data index and descriptor-internal indices wrap as bytes; geometry bases wrap as words.',
                        'All three wheels run interpolation and contact in rear/right/left order; contact clears only bounds bit7.',
                        'The selector makes at most one segment transition per wheel, even for very large forward positions.'],
            'limitations':['Captured track arrays are runtime-generated and are not assumed identical to disk.',
                           'The 0x10100 geometry window is an addressability bound, not a claim that every byte is immutable geometry.',
                           'Source operand names are provisional; the original lateral origin is 0x1bc5e, not reference 0x1bc5c.',
                           'This recovers selector/contact behavior, not full car integration, hardware timing or whole-frame equivalence.']}
    OUT.mkdir(parents=True,exist_ok=True);(OUT/'reference-comparison.json').write_text(json.dumps(record,indent=2)+'\n')
    (OUT/'original-disassembly.asm').write_text('\n'.join(listing)+'\n')
    print(json.dumps({'comparison':str(OUT/'reference-comparison.json'),'routines':len(routines),
                      'bytes':sum(r['size'] for r in routines),'instructions':sum(r['instruction_count'] for r in routines),
                      'known_operand_differences':len(found),'verified_captures':len(captures)},indent=2))
if __name__=='__main__':main()
