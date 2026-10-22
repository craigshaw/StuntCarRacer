"""Verify original local controller, gas and boost logic against pinned source."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

ROOT=Path(__file__).resolve().parents[3]
REF=ROOT/'analysis/reference/vesuri'
BASE=0xe700
ADF_SHA='548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA='508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA='321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT='3cce78d193c3d0208be34d0bbfc3155e787cd406'
SPANS=[('processPlayerInput','waitForTrackPreviewInput',0x5d8a2,0x5d980),
       ('readControllerInput','handleGroundCollision',0x60bae,0x60c86),
       ('updateTurboBoostSystem','renderExistingHole',0x608a4,0x6091a),
       ('updateBoostFuelDisplay','updateTurboBoostSystem',0x60824,0x608a4),
       ('readJoystickState','setupFrameBufferAddresses',0x1b74a,0x1b7b6),
       ('checkKeyPressed','multiplyWithSignHandling',0x612be,0x612ce),
       ('generateRandomNumber','displayMessage',0x62574,0x625a2),
       ('renderDigitAndAdvance','clearGameDataSlot',0x5def6,0x5df0a)]

def sha(data): return hashlib.sha256(data).hexdigest()
def require(condition,message):
    if not condition: raise ValueError(message)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,default=ROOT/'analysis/captures/race')
    parser.add_argument('--output',type=Path,default=ROOT/'analysis/player-controls')
    args=parser.parse_args()
    adf=(ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes()
    require(sha(adf)==ADF_SHA,'ADF changed')
    payload=adf[0xdc00:0xdc00+408540];require(sha(payload)==PAYLOAD_SHA,'Payload changed')
    source=(REF/'StuntCarRacer.s').read_bytes();require(sha(source)==SOURCE_SHA,'Reference changed')
    provenance=json.loads((REF/'provenance.json').read_text());require(provenance['commit']==COMMIT,'Commit changed')
    item=next(f for f in provenance['files'] if f['path']=='StuntCarRacer.s');require(item['sha256']==SOURCE_SHA,'Provenance changed')
    lines=source.decode().splitlines();symbols={name.lower():start for name,_,start,_ in SPANS}
    symbols.update({'rendercharacter':0x594c6,'readinputwithnetworksync':0x57ac0,
                    '_custom':0xdff000,'joy1dat':0xc,'_ciaa':0xbfe001,'ciaddra':0x200,'ccr':'ccr','sr':'sr'})
    for line in lines:
        match=re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)',line,re.I)
        if match: symbols[match[1].lower()]=BASE+int(match[2],16)
    spec=importlib.util.spec_from_file_location('normalizer',Path(__file__).with_name('verify_transform_reference.py'))
    norm=importlib.util.module_from_spec(spec);spec.loader.exec_module(norm)
    def canonical(text,table=None):
        text=norm.canonical(text,table)
        text=re.sub(r'^(b\w+)\.[ws] ',r'\1 ',text)
        text=re.sub(r'^lea\.l ', 'lea ',text)
        text=re.sub(r'^(btst|bset|bclr)\.b ',r'\1 ',text)
        text=re.sub(r'^abcd\.b ', 'abcd ',text)
        text=re.sub(r'(\d+)\+(\d+)',lambda m:str(int(m[1])+int(m[2])),text)
        text=re.sub(r'\b([da])([0-7])-\1([0-7])',lambda m:'/'.join(m[1]+str(n) for n in range(int(m[2]),int(m[3])+1)),text)
        text=re.sub(r'(?<!\d)0\(a([0-7])\)',r'(a\1)',text)
        return text
    capture=json.loads((args.capture/'manifest.json').read_text());bank=next(b for b in capture['banks'] if b['tag']=='CRAM')
    chip=(args.capture/bank['file']).read_bytes()
    require(int(bank['base'],0)==0 and len(chip)==bank['size']==0x80000,'Unexpected RAM mapping')
    require(sha(chip)==bank['sha256'],'Capture RAM changed')
    require(sha(Path(capture['source_path']).read_bytes())==capture['source_sha256'],'USS changed')
    decoder=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000)
    routines=[];listing=[]
    for name,next_name,start,end in SPANS:
        data=payload[start-BASE:end-BASE];require(chip[start:end]==data,name+' differs in capture')
        decoded=list(decoder.disasm(data,start));require(sum(i.size for i in decoded)==len(data),'Incomplete decode '+name)
        first=lines.index(name+':')+1
        if name=='checkKeyPressed':
            # The next function has two entry labels; this RTS is its full span.
            last=next(n for n in range(first,len(lines)) if lines[n].strip()=='RTS')+1
        else: last=lines.index(next_name+':')
        selected=[];labels={};removed=[]
        for number in range(first,last):
            text=lines[number].split(';')[0].strip()
            if not text: continue
            if name=='readJoystickState' and (text.lower().startswith('ifeq') or text.lower() in ('jsr\ttestquit','endc')):
                removed.append({'source_line':number+1,'source':text});continue
            if name=='updateBoostFuelDisplay' and (text.lower().startswith('tst.l') or text.lower()=='bne.s\t.boostok'):
                removed.append({'source_line':number+1,'source':text});continue
            if ':' in text:
                label,text=text.split(':',1);index=len(selected)
                labels[label.lower()]=decoded[index].address if index<len(decoded) else end;text=text.strip()
            if text:selected.append((number+1,text))
        require(len(selected)==len(decoded),f'{name} instruction count {len(selected)} != {len(decoded)}')
        instructions=[];differences=[]
        for ins,(number,text) in zip(decoded,selected):
            resolved=text.lower()
            for label,address in sorted(labels.items(),key=lambda x:-len(x[0])):resolved=resolved.replace(label,str(address))
            actual=canonical(ins.mnemonic+' '+ins.op_str);expected=canonical(resolved,symbols)
            if actual!=expected:
                require(ins.address==0x60bbc and actual=='move.l #396424,a4' and expected=='move.l #391994,a4',
                        f'{name} {ins.address:#x}: {actual} != {expected}')
                differences.append({'address':hex(ins.address),'original':actual,'source':expected,
                                    'reason':'Original controlKeys address0x60c88 differs from enhanced equ0x5fb3a.'})
            instructions.append({'address':hex(ins.address),'bytes':ins.bytes.hex(),'original':ins.mnemonic+' '+ins.op_str,
                                 'source_line':number,'source':text})
        record={'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'size':len(data),
                'payload_offset':hex(start-BASE),'disk_offset':hex(start-BASE+0xdc00),'sha256':sha(data),
                'capture_equals_original':True,'instruction_count':len(decoded),'operand_wildcards':0,
                'source_differences':differences,'enhanced_only_source':removed,'instructions':instructions}
        routines.append(record);listing.append('; '+name+' SHA256 '+sha(data))
        listing.extend(f'{i.address:#x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}' for i in decoded);listing.append('')
    # Execute this untouched renderer in the oracle. Pixels and its text state
    # are excluded from native gameplay comparison, not replaced by fake RTSs.
    start,end=0x594c6,0x59806;data=payload[start-BASE:end-BASE]
    require(chip[start:end]==data,'Renderer code changed')
    routines.append({'name':'original primary-font renderer, oracle-only','runtime_address':hex(start),
                     'end_exclusive':hex(end),'size':len(data),'sha256':sha(data),'capture_equals_original':True,
                     'native_scope':'Excluded graphics dependency; original instructions execute in oracle.'})
    record={'source_adf_sha256':ADF_SHA,'payload_sha256':PAYLOAD_SHA,'reference_commit':COMMIT,
            'reference_source_sha256':SOURCE_SHA,'reference_url':item['url'],'routines':routines,'tables':[],
            'capture':{'directory':str(args.capture.resolve()),'ram_sha256':sha(chip),'source_sha256':capture['source_sha256']},
            'hardware':{'read_word':'0xdff00c JOY1DAT','read_bytes':['0xbfe001 CIA-A PRA','0xbfe201 CIA-A DDRA'],
                        'write_byte':'0xbfe201 := DDRA &0x7f only on joystick fallback'},
            'runtime_data':{'keyboard':'0xead6,128 bytes, pressed iff0xb3','keys':'0x60c88,5 bytes, runtime configurable',
                            'shared_rng':'0x625a2 longword and0x625a6 tail','network_mode':'0x57c3c must be0 for this local API'},
            'boundaries':['Network serial handshake branch is unsupported and rejected, not stubbed.',
                          'Native fuel display is a redraw event; original primary-font renderer executes during differential tests.',
                          'HUD framebuffer/text outputs are excluded from gameplay equivalence; controller/RNG/fuel outputs and hardware effect are compared.'],
            'entry_condition':'D1/D2 bits8..15 zero; local mode; valid control key indices below128.',
            'quirks':['Nonzero countdown timer itself becomes steeringDirection when wheels are active.',
                      'Keyboard controls take priority over all joystick input; joystick state stays unchanged when keyboard is used.',
                      'Acceleration remains latched when no up/down input is held.',
                      'Gas output word aliases accumulator high byte and intensity low byte; boost shifts both together.',
                      'Boost may consume fuel while countdown/speed cap zeros gas; final fuel unit still boosts current update.',
                      'Fuel timer decrements/wraps before a signed-byte test; fuel BCD update clears X before ABCD.']}
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'reference-comparison.json').write_text(json.dumps(record,indent=2)+'\n')
    (args.output/'original-disassembly.asm').write_text('\n'.join(listing))
    print(json.dumps({'report':str(args.output/'reference-comparison.json'),'instruction_count':sum(r.get('instruction_count',0) for r in routines),
                      'operand_wildcards':0,'source_differences':sum(len(r.get('source_differences',[])) for r in routines)},indent=2))

if __name__=='__main__':main()
