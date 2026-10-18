"""Verify motion-transform original bytes, selectors and pinned reference operands."""
from pathlib import Path
import argparse
import hashlib
import json
import re
from capstone import Cs,CS_ARCH_M68K,CS_MODE_BIG_ENDIAN,CS_MODE_M68K_000

ROOT=Path(__file__).resolve().parents[2];REF=ROOT/'analysis/reference/vesuri';OUT=ROOT/'analysis/motion-transforms'
BASE,DISK,SIZE=0xe700,0xdc00,408540
ADF_SHA='548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA='508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA='321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT='3cce78d193c3d0208be34d0bbfc3155e787cd406'
ROUTINES=[('getNegativeTrigValue',0x61338,0x61340,'getPositiveTrigValue'),
          ('getPositiveTrigValue',0x61340,0x61344,'matrixMultiply'),
          ('matrixMultiply',0x61344,0x6135a,'squareAndAbsolute'),
          ('transformWorldCoordinates',0x6158c,0x615e6,'calculateViewAngles'),
          ('calculateViewAngles',0x615e6,0x61618,'calculateSecondaryCoordinates'),
          ('calculateSecondaryCoordinates',0x61618,0x61672,'calculateTertiaryCoordinates'),
          ('calculateTertiaryCoordinates',0x61672,0x616d6,'verticalBlank')]
FIELDS=[('worldXSpeed',0x1bcea),('worldYSpeed',0x1bcec),('worldZSpeed',0x1bcee),
        ('rotationSpeedX',0x1bcf0),('rotationSpeedY',0x1bcf2),('rotationSpeedZ',0x1bcf4),
        ('worldAccelerationX',0x1bcf6),('worldAccelerationY',0x1bcf8),('worldAccelerationZ',0x1bcfa),
        ('baseTargetRoll',0x1bd0e),('baseRollAngle',0x1bd10),('additionalYawOffset',0x1bd12),
        ('carLocalVelocityX',0x1bd2c),('carLocalVelocityY',0x1bd2e),('carLocalVelocityZ',0x1bd30),
        ('adjustedRollAngle',0x1bd32),('adjustedPitchAngle',0x1bd34),('adjustedYawAngle',0x1bd36),
        ('transformedAngularVelocityX',0x1bd3a),('transformedAngularVelocityY',0x1bd3c),('transformedAngularVelocityZ',0x1bd3e)]
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
    o=re.sub(r'(?<![\w])(\d+)\.[wl]\b',r'\1',o);o=re.sub(r'(?<![\w])0\(','(',o)
    m=re.sub(r'^(move|add|sub|cmp)a\.',r'\1.',m);m=re.sub(r'^(add|sub|cmp)i\.',r'\1.',m)
    m=re.sub(r'^(muls)\.w$',r'\1',m);m=re.sub(r'^(bra|bpl)\.[ws]$',r'\1',m)
    return m+' '+re.sub(r'\s+','',o)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--capture',action='append',type=Path);args=ap.parse_args()
    adf=(ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes();require(sha(adf)==ADF_SHA,'ADF hash')
    payload=adf[DISK:DISK+SIZE];require(sha(payload)==PAYLOAD_SHA,'Payload hash')
    require((REF/'offline-extracted-payload.bin').read_bytes()==payload,'Retained payload identity')
    p=json.loads((REF/'provenance.json').read_text(encoding='utf-8-sig'));require(p['commit']==COMMIT,'Reference commit')
    f=next(f for f in p['files'] if f['path']=='StuntCarRacer.s');source=(REF/f['path']).read_bytes()
    require(sha(source)==SOURCE_SHA==f['sha256'],'Source hash');lines=source.decode().splitlines();symbols={}
    for line in lines:
        m=re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)',line,re.I)
        if m:symbols[m[1].lower()]=BASE+int(m[2],16)
        m=re.match(r'^(lbC[0-9a-f]+):',line,re.I)
        if m:symbols[m[1].lower()]=BASE+int(m[1][3:],16)
    symbols.update({n.lower():a for n,a,_,_ in ROUTINES})
    for n,a in FIELDS:require(symbols[n.lower()]==a,'Reference field address '+n)
    require(symbols['transformationmatrix']==0x1ec46 and symbols['lbl00db30']==0x1c230,'Selector/coefficient addresses')
    c=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000);routines=[];listing=[]
    for name,start,end,next_name in ROUTINES:
        code=payload[start-BASE:end-BASE];decoded=list(c.disasm(code,start));require(sum(i.size for i in decoded)==len(code),'Decode extent')
        first=lines.index(name+':');last=lines.index(next_name+':');ins=[]
        for line in range(first+1,last):
            text=lines[line].split(';')[0].strip()
            if text and not text.endswith(':'):ins.append((line+1,text))
        require(len(ins)==len(decoded),'Source instruction count '+name);records=[]
        for (line,text),i in zip(ins,decoded):
            actual=i.mnemonic+' '+i.op_str
            require(canonical(text,symbols)==canonical(actual),f'{i.address:#x}: {text} != {actual}')
            records.append({'address':hex(i.address),'bytes':i.bytes.hex(),'original':actual,'source_line':line,'source':text})
            listing.append(f'{i.address:08x} {i.bytes.hex():24s} {actual}')
        routines.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'payload_offset':hex(start-BASE),
                         'disk_offset':hex(DISK+start-BASE),'size':len(code),'sha256':sha(code),'instruction_count':len(decoded),
                         'source_line':first+1,'source_matches_all_instructions_and_operands':True,'operand_wildcards':0,'instructions':records})
    selectors=payload[0x1ec46-BASE:0x1ec5c-BASE]
    require(sha(selectors)=='299c81f4b390404956f67f82d826358f533badec72533fa2cced765c19f69c17','Selector table hash')
    table={'name':'motionCoefficientSelectors','reference_name':'transformationMatrix','runtime_address':'0x1ec46','end_exclusive':'0x1ec5c',
           'payload_offset':hex(0x1ec46-BASE),'disk_offset':hex(DISK+0x1ec46-BASE),'size':22,'sha256':sha(selectors),
           'bounds_evidence':'Largest indexed byte is 0x14+1 in tertiary transform; first indexed byte is zero in world transform.'}
    captures=[]
    for path in args.capture or [ROOT/'analysis/captures/race']:
        m=json.loads((path/'manifest.json').read_text());b=next(b for b in m['banks'] if b['tag']=='CRAM');ram=(path/b['file']).read_bytes()
        require(b['base']=='0x0' and len(ram)==b['size']==0x80000 and sha(ram)==b['sha256'],'Capture RAM identity')
        require(sha(Path(m['source_path']).read_bytes())==m['source_sha256'],'Source savestate identity')
        for _,a,z,_ in ROUTINES:require(ram[a:z]==payload[a-BASE:z-BASE],'Capture original code identity')
        require(ram[0x1ec46:0x1ec5c]==selectors,'Capture selector table differs')
        captures.append({'directory':str(path.resolve()),'ram_sha256':sha(ram),'savestate_sha256':m['source_sha256'],
                         'code_and_selector_bytes_equal_disk':True,'pc':m['cpu']['registers']['PC'],
                         'fields':{n:int.from_bytes(ram[a:a+2],'big') for n,a in FIELDS},
                         'coefficient_words':[int.from_bytes(ram[a:a+2],'big') for a in range(0x1c230,0x1c278,2)]})
    report={'source_adf_sha256':ADF_SHA,'payload_sha256':PAYLOAD_SHA,'payload_base':hex(BASE),'payload_disk_offset':hex(DISK),
            'reference':{'repository':p['repository'],'commit':COMMIT,'source_url':f['url'],'source_sha256':SOURCE_SHA},
            'routines':routines,'tables':[table],'captures':captures,'fields':[{'name':n,'address':hex(a),'size':2} for n,a in FIELDS],
            'source_differences':[],'hardware_accesses':[],
            'behavior':['Signed multiplication is doubled modulo32, SWAP exposes result word; each term truncates before word-wrapped addition.',
                        'World transform updates only X/Z and retains the local Y word.',
                        'View helper names do not imply sine/cosine: they multiply selected coefficients by -317/+317.',
                        'Secondary transform writes three outputs from nine independently rounded products.',
                        'Tertiary X/Y each sum two products; Z uses new transformed Y times coefficient4 plus raw rotation Z.'],
            'register_contract':['World/secondary/tertiary read selector bytes into D1; D1 bits8..15 must initially be zero.',
                                 'Matrix helper shifts D1.W left then right; selected indices0..35 preserve the valid word and all upper16 bits.',
                                 'View stage initializes the whole D1 word before each helper and has no D1 high-byte precondition.',
                                 'Producer, wheel and expected-height code do not touch D1; selector preserves zero highbyte and returns lowbyte0xfe.',
                                 'D2 is initialized with MOVE.W at each coordinate stage; other old registers and CCR do not affect outputs.'],
            'limitations':['Native selector domain is the36word coefficient block; original arbitrary D1 index/address effects are outside contract.',
                           'Existing source axis/angle labels are retained provisionally; physical units need whole-game investigation.',
                           'Snapshot current input and stored output can belong to different updates.',
                           'CPU register side effects, stack placement and timing are outside native output contract.']}
    OUT.mkdir(parents=True,exist_ok=True);(OUT/'reference-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'original-disassembly.asm').write_text('\n'.join(listing)+'\n')
    print(json.dumps({'comparison':str(OUT/'reference-comparison.json'),'routines':len(routines),'code_bytes':sum(r['size'] for r in routines),
                      'instructions':sum(r['instruction_count'] for r in routines),'selector_bytes':len(selectors),
                      'verified_captures':len(captures),'operand_wildcards':0,'source_differences':0},indent=2))
if __name__=='__main__':main()
