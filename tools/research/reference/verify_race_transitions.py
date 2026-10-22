"""Pin original local restart, quit, pause, race-end and buffer-swap instructions.

Decode the verified payload as MC68000 and compare all source operands after
explicitly reverting the pinned enhanced source's frame-scheduling changes.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re
from capstone import Cs,CS_ARCH_M68K,CS_MODE_BIG_ENDIAN,CS_MODE_M68K_000

ROOT=Path(__file__).resolve().parents[3]
REF=ROOT/'analysis/reference/vesuri'
BASE=0xe700
ADF_SHA='548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06'
PAYLOAD_SHA='508646f12af15115de73b06940c79e579f920bf47ce55f7cd8920eca60b7921c'
SOURCE_SHA='321034a19432bc23f2373acb68a121ee46dc4c5ed382e6df30ee05aff3c7924a'
COMMIT='3cce78d193c3d0208be34d0bbfc3155e787cd406'
def sha(data):return hashlib.sha256(data).hexdigest()
def require(value,message):
    if not value:raise ValueError(message)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,default=ROOT/'analysis/captures/race')
    parser.add_argument('--output',type=Path,default=ROOT/'analysis/race-transitions')
    args=parser.parse_args()
    adf=(ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes();require(sha(adf)==ADF_SHA,'ADF changed')
    payload=adf[0xdc00:0xdc00+408540];require(sha(payload)==PAYLOAD_SHA,'Payload changed')
    source=(REF/'StuntCarRacer.s').read_bytes();require(sha(source)==SOURCE_SHA,'Reference source changed')
    provenance=json.loads((REF/'provenance.json').read_text());require(provenance['commit']==COMMIT,'Reference commit changed')
    item=next(f for f in provenance['files'] if f['path']=='StuntCarRacer.s');require(item['sha256']==SOURCE_SHA,'Reference provenance changed')
    lines=source.decode().splitlines()
    symbols={}
    for line in lines:
        match=re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)',line,re.I)
        if match:symbols[match[1].lower()]=BASE+int(match[2],16)
        match=re.match(r'^(lbC[0-9a-f]+):',line,re.I)
        if match:symbols[match[1].lower()]=BASE+int(match[1][3:],16)
    symbols.update({'mainGameLoop'.lower():0x5d47e,'continuegameloop':0x5d5a0,'checkgameendconditions':0x5d4e8,
        'renderplayer2cargraphic':0x5e0aa,'rendermessagepanel':0x5fae8,'displaygamemessage':0x5e502,
        'renderexistinghole':0x6091a,'swapdisplaybuffers':0x5dac0,'handlepause':0x5d9dc,
        'updatenetworkgameflags':0x57964,'disableaudio':0xf3f0,'waitforfirebuttonpress':0x6488a,
        'initializegamesystemsandmainloop':0x5d402,'checkkeypressedornetworkstate':0x57100,
        'setmessageparameters':0x5e4ec,'updategamemessagedisplay':0x5e508,'waitforunpause':0x57b86,
        'playsample':0xf362,'updateengineaudiopitch':0x5b56c,'setupframebufferaddresses':0x1b7b6,
        'fadetocolor':0x6444e,'saverandomstate':0x6368c,'cleargamedataslot':0x5df0a,
        'wheelSpeed'.lower():0x1bc62,'major_impact_cooldown_time':0x45})
    # 0x1bc60 is an enhanced-reference insertion, not this disk's speed word.
    spec=importlib.util.spec_from_file_location('normalizer',Path(__file__).with_name('verify_transform_reference.py'))
    norm=importlib.util.module_from_spec(spec);spec.loader.exec_module(norm)
    def canonical(text,table=None):
        return re.sub(r'^(b\w+)\.[ws] ',r'\1 ',norm.canonical(text,table))
    gate_first=next(n for n in range(lines.index('mainGameLoop:')+1,len(lines)) if lines[n].strip()=='MOVE.B\tframeBufferSyncMask,D0')
    spans=[('race-end gate',gate_first,lines.index('showFramerate:'),0x5d4ba,0x5d5a0),
        ('restart and quit',lines.index('continueGameLoop:')+1,lines.index('lbC04EF74:'),0x5d5a0,0x5d674),
        ('pause and resume',lines.index('handlePause:')+1,lines.index('initializeGameLoop:'),0x5d9dc,0x5daac),
        ('engine resume boundary',lines.index('initializeGameLoop:')+1,lines.index('swapDisplayBuffers:'),0x5daac,0x5dabe),
        ('buffer swap',lines.index('swapDisplayBuffers:')+1,lines.index('updateFrameThrottlingAndTimers:'),0x5dac0,0x5db34),
        ('buffer pointers',lines.index('setupFrameBufferAddresses:')+1,lines.index('updateCopperlistPointers:'),0x1b7b6,0x1b7dc),
        ('common race exit',lines.index('lbC04EF74:')+1,lines.index('loadPlayerConfiguration:'),0x5d674,0x5d724),
        ('save random state',lines.index('saveRandomState:')+1,lines.index('initializeRandomSeeds:'),0x6368c,0x636a2)]
    capture=json.loads((args.capture/'manifest.json').read_text());bank=next(b for b in capture['banks'] if b['tag']=='CRAM')
    chip=(args.capture/bank['file']).read_bytes();require(int(bank['base'],0)==0 and len(chip)==bank['size']==0x80000,'RAM mapping')
    require(sha(chip)==bank['sha256'],'RAM changed');require(sha(Path(capture['source_path']).read_bytes())==capture['source_sha256'],'USS changed')
    decoder=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000);routines=[];listing=[];changes=[]
    for name,first,last,start,end in spans:
        data=payload[start-BASE:end-BASE];require(chip[start:end]==data,name+' captured code differs')
        decoded=list(decoder.disasm(data,start));require(sum(i.size for i in decoded)==len(data),'Incomplete decode '+name)
        selected=[];local={};removed=[]
        for index in range(first,last):
            raw=lines[index];text=raw.split(';')[0].strip()
            if not text:continue
            if 'frameProcessingFlag' in text or 'framesSinceCopperlistUpdate' in text:
                removed.append({'line':index+1,'source':raw});continue
            if name=='buffer pointers' and (text.startswith('.wait:') or text.lower()=='bmi\t.wait'):
                removed.append({'line':index+1,'source':raw,'reason':'Enhanced-only copper wait.'});continue
            if ':' in text:
                label,text=text.split(':',1);local[label.lower()]=decoded[len(selected)].address if len(selected)<len(decoded) else end;text=text.strip()
                if not text:continue
            original_text=text
            if name=='save random state' and re.search(r'MOVE\.B\s+randomSeed3,savedRandomSeed3',text,re.I):
                text=re.sub(r'savedRandomSeed3',r'$636b9',text,flags=re.I)
            if name=='buffer pointers' and re.search(r'MOVE\.L\s+#fastRenderBuffer,D3',text,re.I):text=re.sub(r'#fastRenderBuffer','renderFrameBuffer',text,flags=re.I)
            if name=='buffer swap' and text.upper()=='RTS':
                removed.append({'line':index+1,'source':raw,'reason':'Original tail-jumps to setupFrameBufferAddresses.'});continue
            if name=='buffer swap' and re.search(r'JSR\s+setupFrameBufferAddresses',text,re.I):text=re.sub(r'^JSR','JMP',text,flags=re.I)
            if name=='restart and quit' and re.search(r'\b(SUB|ADD)\.B\s+d0,restartTimerCountdown',text,re.I):
                text=re.sub(r'\b(SUB|ADD)\.B\s+d0,',lambda m:m[1]+'Q.B #$01,',text,flags=re.I)
            if name=='buffer swap' and re.search(r'MOVE\.B\s+#\$00,framesToWait',text,re.I):text=re.sub(r'#\$00,framesToWait','#$06,framesToWait',text,flags=re.I)
            if text!=original_text:changes.append({'source_line':index+1,'enhanced_source':original_text,'restored_original':text})
            selected.append((index+1,text))
        require(len(selected)==len(decoded),f'{name}: {len(selected)} source instructions !={len(decoded)} original')
        table={**symbols,**local};instructions=[]
        for ins,(number,text) in zip(decoded,selected):
            resolved=text.lower()
            for label,address in sorted(local.items(),key=lambda p:-len(p[0])):resolved=resolved.replace(label,str(address))
            actual=canonical(ins.mnemonic+' '+ins.op_str);expected=canonical(resolved,table)
            require(actual==expected,f'{name} {ins.address:#x}: {actual} != {expected} ({text})')
            instructions.append({'address':hex(ins.address),'bytes':ins.bytes.hex(),'original':ins.mnemonic+' '+ins.op_str,'source_line':number,'source':text})
        routines.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'payload_offset':hex(start-BASE),
            'disk_offset':hex(start-BASE+0xdc00),'size':len(data),'sha256':sha(data),'capture_equals_original':True,
            'instruction_count':len(decoded),'operand_wildcards':0,'enhanced_only_source_removed':removed,'instructions':instructions})
        listing.append('; '+name+' SHA256 '+sha(data));listing.extend(f'{i.address:#x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}' for i in decoded);listing.append('')
    callees=[]
    for name,start,end in [('key query or network flag',0x57100,0x57122),('local key equality',0x612be,0x612ce),
            ('local network flag no-op',0x57964,0x579b0),('disable audio hardware boundary',0xf3f0,0xf402),
            ('set message fields',0x5e4ec,0x5e508),('player results icon setup',0x5e0aa,0x5e0c4)]:
        data=payload[start-BASE:end-BASE];require(chip[start:end]==data,name+' captured code differs')
        decoded=list(decoder.disasm(data,start));require(sum(i.size for i in decoded)==len(data),'Incomplete decode '+name)
        callees.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'payload_offset':hex(start-BASE),
            'disk_offset':hex(start-BASE+0xdc00),'sha256':sha(data),'capture_equals_original':True,
            'scope':'Supplemental original-byte evidence; no enhanced-source operand comparison claimed.',
            'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'original':i.mnemonic+' '+i.op_str} for i in decoded]})
        listing.append('; '+name+' SHA256 '+sha(data));listing.extend(f'{i.address:#x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}' for i in decoded);listing.append('')
    report={'source_adf_sha256':ADF_SHA,'payload_sha256':PAYLOAD_SHA,'reference_commit':COMMIT,'reference_source_sha256':SOURCE_SHA,
        'reference_url':item['url'],'routines':routines,'explicit_source_changes':changes,'boundary_callee_evidence':callees,
        'capture':{'directory':str(args.capture.resolve()),'ram_sha256':sha(chip),'source_sha256':capture['source_sha256']},
        'native_scope':'Local mode end gates, restart/quit decisions, pause bookkeeping and framebuffer pointers; common exit cleanup and RNG save for Practice (selectedTrackInDivision sign clear). Network unsupported.',
        'oracle_boundaries':['Presentation callees become ordered events; original renderer pixels and renderer scratch are not claimed by this leaf test.',
            'disableAudio is an explicit event for custom-register writes DFF096=0x000f and DFF09a=0x0780.',
            'Fire/unpause waits and six-frame swap delay are caller-supplied boundaries; no interrupt timing is claimed.',
            'The replay runner may stop at fire/fade boundaries; finishLocalRaceExit separately recovers Practice cleanup0x5d674..0x5d724. Championship bookkeeping branch is source-verified but not implemented.',
            'Restart exits before0x5d402.'],
        'quirks':['Restart byte decrements with wrapping; signed negative result is incremented back before blink test, including input0x80..0xff.',
            'Race-end clears only high byte of wheelSpeed0x1bc62, preserving low byte.',
            'Pause press arms0x80 this tick, enters wait on following invocation even if key released.',
            'Original swap waits for zero then writes framesToWait=6. Enhanced reference writes0.',
            'Original saveRandomState copies625a2.long to636b8.long then625a6.byte to636b9.byte, overwriting the second saved seed byte. Enhanced source uses636bc.',
            'Finish gate is checked against pre-swap frameBufferToggle; restart/quit occur after swap and pause.']}
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'reference-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    (args.output/'original-disassembly.asm').write_text('\n'.join(listing))
    print(json.dumps({'report':str(args.output/'reference-comparison.json'),'instruction_count':sum(r['instruction_count'] for r in routines),'operand_wildcards':0,'explicit_source_changes':len(changes)},indent=2))
if __name__=='__main__':main()
