"""Audit original steering, including removed integrity check and added source timestep."""
from pathlib import Path
import argparse
import hashlib
import json
import re
from capstone import Cs,CS_ARCH_M68K,CS_MODE_BIG_ENDIAN,CS_MODE_M68K_000

ROOT=Path(__file__).resolve().parents[3];REF=ROOT/'analysis/reference/vesuri';OUT=ROOT/'analysis/steering-response'
BASE,DISK,SIZE=0xe700,0xdc00,408540
ADF_SHA='548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA='508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA='321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT='3cce78d193c3d0208be34d0bbfc3155e787cd406'
ROUTINES=[('calculateSteeringResponse',0x61012,0x6125a,'displayTrackHeader'),
          ('loadTrackSegmentConfiguration',0x5fe56,0x5ff94,'transformSegmentToViewSpace'),
          ('advanceToNextSegment',0x5c51a,0x5c538,'retreatToPreviousSegment')]
FIELDS=[('playerSegmentIndex',0x1bb1c,1),('trackProgressionByte',0x1bb0a,1),('wheelMovementActive',0x1bb7e,1),
        ('segmentTargetAngle',0x1bd5a,2),('cameraAngleY',0x1bce6,2),('carLocalVelocityZ',0x1bd30,2),
        ('rotationSpeedY',0x1bcf2,2),('angularAccelerationY',0x1bcfe,2),('steeringInputDirection',0x1bbc6,1),
        ('trackDirectionMultiplier',0x1bb5d,1),('segmentProgressDistance',0x1bc2a,2),('steeringScaleFactor',0x1bc3c,2),
        ('tempWord1',0x1bbf6,2),('tempByte3',0x1bb1a,1),('tempByte4',0x1bb1b,1)]
def sha(b):return hashlib.sha256(b).hexdigest()
def require(b,s):
    if not b:raise ValueError(s)
def canonical(text,symbols=None):
    p=text.lower().split(None,1);m=p[0];o=p[1] if len(p)>1 else ''
    o=re.sub(r'\$([0-9a-f]+)',lambda x:str(int(x[1],16)),o)
    if symbols is not None:
        def resolve(x):
            t=x[0]
            if t in symbols:return str(symbols[t])
            if re.fullmatch(r'[ad][0-7]|sp|\.?[wl]',t):return t
            raise ValueError('Unresolved operand '+t)
        o=re.sub(r'(?<![\w])\.?[a-z_]\w*',resolve,o)
    o=re.sub(r'(\d+)\+(\d+)',lambda x:str(int(x[1])+int(x[2])),o)
    o=re.sub(r'(?<![\w])(\d+)\.[wl]\b',r'\1',o);o=re.sub(r'(?<![\w])0\(','(',o)
    m=re.sub(r'^(move|add|sub|cmp)a\.',r'\1.',m);m=re.sub(r'^(add|sub|cmp|and)i\.',r'\1.',m)
    m=re.sub(r'^(muls)\.w$',r'\1',m);m=re.sub(r'^(b\w+)\.[ws]$',r'\1',m);m=re.sub(r'^(bclr)\.b$',r'\1',m)
    return m+' '+re.sub(r'\s+','',o)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--capture',type=Path,action='append');args=ap.parse_args()
    adf=(ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes();require(sha(adf)==ADF_SHA,'ADF identity');payload=adf[DISK:DISK+SIZE]
    require(sha(payload)==PAYLOAD_SHA and (REF/'offline-extracted-payload.bin').read_bytes()==payload,'Payload identity')
    p=json.loads((REF/'provenance.json').read_text(encoding='utf-8-sig'));require(p['commit']==COMMIT,'Reference commit')
    f=next(f for f in p['files'] if f['path']=='StuntCarRacer.s');source=(REF/f['path']).read_bytes();require(sha(source)==SOURCE_SHA==f['sha256'],'Source hash')
    lines=source.decode().splitlines();symbols={'start':BASE,'memory_79360':0x79360}
    for line in lines:
        m=re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)',line,re.I)
        if m:symbols[m[1].lower()]=BASE+int(m[2],16)
        m=re.match(r'^(lbC[0-9a-f]+):',line,re.I)
        if m:symbols[m[1].lower()]=BASE+int(m[1][3:],16)
    symbols.update({n.lower():a for n,a,_,_ in ROUTINES})
    for n,a,_ in FIELDS:require(symbols[n.lower()]==a,'Field address '+n)
    require(symbols['steeringoffsettable']==0x6125a,'Steering offset table')
    c=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000);routines=[];listing=[];differences=[]
    for name,a,z,next_name in ROUTINES:
        code=payload[a-BASE:z-BASE];decoded=list(c.disasm(code,a));require(sum(i.size for i in decoded)==len(code),'Code extent '+name)
        first=lines.index(name+':');last=lines.index(next_name+':');ins=[];local=dict(symbols);skip_asr=False
        for line in range(first+1,last):
            raw=lines[line];text=raw.split(';')[0].strip();restored=False
            if name=='calculateSteeringResponse':
                if 'muls' in text.lower() and '#TIMESTEP_FACTOR' in text:
                    require('; added' in raw,'Expected annotated added timestep multiply')
                    differences.append({'kind':'enhanced_source_added_instruction','source_line':line+1,'source':text});skip_asr=True;continue
                if skip_asr:
                    require(canonical(text)=='asr.l #8,d0','Expected added timestep ASR')
                    differences.append({'kind':'enhanced_source_added_instruction','source_line':line+1,'source':text});skip_asr=False;continue
                if raw.startswith(';'):
                    text=raw[1:].split(';')[0].strip();restored=True
            if not text:continue
            if text.endswith(':'):local[text[:-1].lower()]=decoded[len(ins)].address;continue
            ins.append((line+1,text,restored))
        require(len(ins)==len(decoded),f'{name} instruction count {len(ins)} vs {len(decoded)}');records=[]
        for (line,text,restored),i in zip(ins,decoded):
            original=i.mnemonic+' '+i.op_str
            require(canonical(text,local)==canonical(original),f'{i.address:#x}: {text} != {original}')
            record={'address':hex(i.address),'bytes':i.bytes.hex(),'original':original,'source_line':line,'source':text}
            if restored:
                record['reference_comments_out_original']=True
                differences.append({'kind':'enhanced_source_commented_out_original','address':hex(i.address),'source_line':line,'source':text})
            records.append(record);listing.append(f'{i.address:08x} {i.bytes.hex():24s} {original}')
        routines.append({'name':name,'runtime_address':hex(a),'end_exclusive':hex(z),'payload_offset':hex(a-BASE),'disk_offset':hex(DISK+a-BASE),
                         'size':len(code),'sha256':sha(code),'instruction_count':len(decoded),'operand_wildcards':0,
                         'source_original_instructions_match_after_documented_restoration':True,'instructions':records})
    require(sum(d['kind']=='enhanced_source_added_instruction' for d in differences)==2,'Expected two added timestep instructions')
    require(sum(d['kind']=='enhanced_source_commented_out_original' for d in differences)==6,'Expected six removed integrity instructions')
    table=payload[0x6125a-BASE:0x61260-BASE]
    captures=[]
    for path in args.capture or [ROOT/'analysis/captures/race']:
        m=json.loads((path/'manifest.json').read_text());b=next(b for b in m['banks'] if b['tag']=='CRAM');ram=(path/b['file']).read_bytes()
        require(b['base']=='0x0' and len(ram)==b['size']==0x80000 and sha(ram)==b['sha256'],'Capture RAM identity')
        require(sha(Path(m['source_path']).read_bytes())==m['source_sha256'],'Source savestate identity')
        for _,a,z,_ in ROUTINES:require(ram[a:z]==payload[a-BASE:z-BASE],'Capture code mismatch')
        require(ram[0x6125a:0x61260]==table,'Capture steering-offset table differs')
        captures.append({'directory':str(path.resolve()),'ram_sha256':sha(ram),'savestate_sha256':m['source_sha256'],
                         'code_and_table_bytes_equal_disk':True,'segment_count':ram[0x1ca1a],'player_segment':ram[0x1bb1c],
                         'integrity_word':hex(int.from_bytes(ram[0x64aec:0x64af0],'big')),
                         'integrity_matches_expected_runtime_value':ram[0x64aec:0x64af0]==bytes.fromhex('9cedcd02'),
                         'reverse_direction_low_byte':ram[0x1bc33],'inversion_low_byte_read_but_no_output_effect':ram[0x1bc45],
                         'fields':{n:int.from_bytes(ram[a:a+s],'big') for n,a,s in FIELDS}})
    report={'source_adf_sha256':ADF_SHA,'payload_sha256':PAYLOAD_SHA,'payload_base':hex(BASE),'payload_disk_offset':hex(DISK),
            'reference':{'repository':p['repository'],'commit':COMMIT,'source_url':f['url'],'source_sha256':SOURCE_SHA},
            'routines':routines,'tables':[{'name':'steeringOffsetTable','runtime_address':'0x6125a','end_exclusive':'0x61260',
              'payload_offset':hex(0x6125a-BASE),'disk_offset':hex(DISK+0x6125a-BASE),'size':6,'sha256':sha(table)}],
            'source_differences':differences,'captures':captures,'hardware_accesses':[],
            'runtime_data':[{'name':'integrity_word','runtime_address':'0x64aec','size':4,'original_disk_hex':payload[0x64aec-BASE:0x64af0-BASE].hex(),
                             'expected_loaded_hex':'9cedcd02','meaning':'Original integrity comparison input, modified during loading; not an immutable disk table.'}],
            'fields':[{'name':n,'address':hex(a),'size':s} for n,a,s in FIELDS],
            'behavior':['Initial full-word direction XOR includes retained byte0x1bc33; configuration updates only byte0x1bc32.',
                        'Steering offsets depend on original segment flags; near-end segment advance occurs after initial angle delta/scale calculation.',
                        'Automatic yaw correction is unscaled in original; enhanced-source timestep multiplication is excluded.',
                        'Byte-scaled products preserve signed multiplication, per-operation word wrapping and logical/arithmetic shift differences.',
                        'Yaw acceleration is gated by nonzero wheelMovementActive and runtime integrityWord ==0x9cedcd02.',
                        'tempByte3/4 are conditionally written and retained on paths that do not write them.'],
            'limitations':['Normal original entry assumes D1/D2 bits8..15 zero; native indices enforce valid1..100 loaded-segment domain.',
                           'Menu snapshots with segmentCount0 cannot enter the steering stage under this contract.',
                           'Runtime integrity-word mutation is evidence from captures; this verifier does not infer the entire loader mutation path.',
                           'CPU flags/registers, timing, hardware and whole-game fidelity remain outside native output contract.']}
    OUT.mkdir(parents=True,exist_ok=True);(OUT/'reference-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'original-disassembly.asm').write_text('\n'.join(listing)+'\n')
    print(json.dumps({'comparison':str(OUT/'reference-comparison.json'),'routines':len(routines),'code_bytes':sum(r['size'] for r in routines),
                      'instructions':sum(r['instruction_count'] for r in routines),'source_differences':len(differences),'verified_captures':len(captures)},indent=2))
if __name__=='__main__':main()
