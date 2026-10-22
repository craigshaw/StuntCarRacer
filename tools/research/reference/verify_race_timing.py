"""Pin original local race scheduling/lap bookkeeping and all source operands."""
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
SPANS=[
 ('updateFrameThrottlingAndTimers','updateBlinkCountdownTimer',0x5db34,0x5dbd6),
 ('updateBlinkCountdownTimer','compareAndCopyPlayerStats',0x5dbd6,0x5dc38),
 ('compareAndCopyPlayerStats','handleLapCompletionTiming',0x5dc38,0x5dca6),
 ('handleLapCompletionTiming','checkLapCompletion',0x5dca6,0x5dcc8),
 ('checkLapCompletion','checkRaceCompletion',0x5dcc8,0x5de1c),
 ('checkRaceCompletion','setTextPosition',0x5de1c,0x5deb6),
 ('refreshLapTimerHUD','renderDigitAndAdvance',0x5ded0,0x5def6),
 ('renderDigitAndAdvance','clearGameDataSlot',0x5def6,0x5df0a),
 ('clearGameDataSlot','updateLapTimer',0x5df0a,0x5df2e),
 ('updateLapTimer','incrementLapTimeBCD',0x5df2e,0x5df32),
 ('incrementLapTimeBCD','updateDamageAndTimers',0x5df32,0x5dfb4),
 ('saveLapTimeToBuffer','synchronizeRaceData',0x5865a,0x5867a),
 ('togglePlayerContext','lbC0499D8',0x58074,0x580d6),
 ('updateRaceRecords','copyPlayerNameToMenu',0x5f16c,0x5f21e),
 ('copyPlayerNameToMenu','transferLapRecords',0x5f21e,0x5f25a),
 ('addLapTimesBCD','randomizeCarAssignments',0x5ee04,0x5ee68),
 ('updateOpponentLapTime','updateGamePhysics',0x6181e,0x6185c),
 ('multiplyAndRandomize','checkKeyPressed',0x612a2,0x612be),
 ('checkForTieCondition','updateBoostFuelDisplay',0x607d0,0x60824),
 ('renderResultsCarIcon','renderDistanceDisplay',0x5e098,0x5e12a),
 ('renderLapTimeDisplay','displayHUDText',0x5e326,0x5e46c),
 ('setMessageParameters','displayGameMessage',0x5e4ec,0x5e502),
 # Supplemental inspection of the surrounding graphics boundary. These are
 # source/byte verified, not implemented as native race-timing functions.
 ('updateGameMessageDisplay','advanceWheelRotationFrame',0x5e508,0x5e65a),
 ('setBackgroundColor','setForegroundColor',0x59806,0x5981a),
 ('setupBitplaneMasks','outputClippedLine',0x6770a,0x67740)]
def sha(data):return hashlib.sha256(data).hexdigest()
def require(condition,message):
    if not condition:raise ValueError(message)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,default=ROOT/'analysis/captures/race')
    parser.add_argument('--output',type=Path,default=ROOT/'analysis/race-timing')
    args=parser.parse_args()
    adf=(ROOT/'Stunt Car Racer (MicroStyle).adf').read_bytes();require(sha(adf)==ADF_SHA,'ADF changed')
    payload=adf[0xdc00:0xdc00+408540];require(sha(payload)==PAYLOAD_SHA,'Payload changed')
    source=(REF/'StuntCarRacer.s').read_bytes();require(sha(source)==SOURCE_SHA,'Reference changed')
    provenance=json.loads((REF/'provenance.json').read_text());require(provenance['commit']==COMMIT,'Commit changed')
    item=next(f for f in provenance['files'] if f['path']=='StuntCarRacer.s');require(item['sha256']==SOURCE_SHA,'Provenance changed')
    lines=source.decode().splitlines();symbols={name.lower():start for name,_,start,_ in SPANS}
    symbols.update({'rendercharacter':0x594c6,'renderleadercaricon':0x5e0c4,'rendermaskedgraphicsobject':0x69f42,'renderdigit':0x5df00,
                    'timestep_factor':39,'framerate_multiplier':6,'ccr':'ccr','sr':'sr'})
    for line in lines:
        match=re.match(r'^(\w+)\s+equ\s+gameData\+\$([0-9a-f]+)',line,re.I)
        if match:symbols[match[1].lower()]=BASE+int(match[2],16)
        match=re.match(r'^(lbC[0-9a-f]+):',line,re.I)
        if match:symbols[match[1].lower()]=BASE+int(match[1][3:],16)
    spec=importlib.util.spec_from_file_location('normalizer',Path(__file__).with_name('verify_transform_reference.py'))
    norm=importlib.util.module_from_spec(spec);spec.loader.exec_module(norm)
    def canonical(text,table=None):
        text=norm.canonical(text,table)
        text=re.sub(r'^(b\w+)\.[ws] ',r'\1 ',text)
        text=re.sub(r'^(btst|bset|bclr|abcd|sbcd)\.b ',r'\1 ',text)
        text=re.sub(r'\b([da])([0-7])-\1([0-7])',lambda m:'/'.join(m[1]+str(n) for n in range(int(m[2]),int(m[3])+1)),text)
        text=re.sub(r'(\d+)\*(\d+)',lambda m:str(int(m[1])*int(m[2])),text)
        return text
    capture=json.loads((args.capture/'manifest.json').read_text());bank=next(b for b in capture['banks'] if b['tag']=='CRAM')
    chip=(args.capture/bank['file']).read_bytes();require(int(bank['base'],0)==0 and len(chip)==bank['size']==0x80000,'RAM mapping')
    require(sha(chip)==bank['sha256'],'RAM changed');require(sha(Path(capture['source_path']).read_bytes())==capture['source_sha256'],'USS changed')
    decoder=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000)
    routines=[];listing=[]
    changes={0x5db3e:('move.b #238,d0','move.b #39,d0','Original fixed0xee; enhanced PAL0xee/6 (NTSC0xee/7).'),
             0x5dd6a:('move.b #27,d0','move.b #162,d0','Original display duration27 updates; enhanced multiplied by framerate.'),
             0x5df2e:('move.b #19,d0','move.b #20,d0','Original clock addsBCD0x13; enhanced0x14 plus elapsed-frame loop.')}
    restored={7002,7010,7011};added=set(range(7012,7019))|{7271,7272}|set(range(7274,7281))
    for name,next_name,start,end in SPANS:
        data=payload[start-BASE:end-BASE];require(chip[start:end]==data,name+' capture differs')
        decoded=list(decoder.disasm(data,start));require(sum(i.size for i in decoded)==len(data),'Incomplete decode '+name)
        first=lines.index(name+':')+1;last=lines.index(next_name+':');selected=[];labels={};removed=[];commented=[]
        for index in range(first,last):
            number=index+1
            if number in added:
                removed.append({'source_line':number,'source':lines[index]});continue
            line=lines[index].lstrip(';') if number in restored else lines[index]
            if number in restored:commented.append({'source_line':number,'source':lines[index]})
            text=line.split(';')[0].strip()
            if not text:continue
            if ':' in text:
                label,text=text.split(':',1);count=len(selected);labels[label.lower()]=decoded[count].address if count<len(decoded) else end;text=text.strip()
            if text:selected.append((number,text))
        require(len(selected)==len(decoded),f'{name}: {len(selected)} source instructions vs{len(decoded)} original')
        instructions=[];differences=[]
        for ins,(number,text) in zip(decoded,selected):
            resolved=text.lower()
            for label,address in sorted(labels.items(),key=lambda p:-len(p[0])):resolved=resolved.replace(label,str(address))
            actual=canonical(ins.mnemonic+' '+ins.op_str);expected=canonical(resolved,symbols)
            if ins.address in changes:
                original,enhanced,reason=changes[ins.address];require(actual==original and expected==enhanced,f'{name} documented difference {actual} vs{expected}')
                differences.append({'address':hex(ins.address),'original':actual,'enhanced':expected,'reason':reason})
            else:require(actual==expected,f'{name} {ins.address:#x}: {actual} != {expected}')
            instructions.append({'address':hex(ins.address),'bytes':ins.bytes.hex(),'original':ins.mnemonic+' '+ins.op_str,'source_line':number,'source':text})
        routines.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'payload_offset':hex(start-BASE),
            'disk_offset':hex(start-BASE+0xdc00),'size':len(data),'sha256':sha(data),'capture_equals_original':True,
            'instruction_count':len(decoded),'operand_wildcards':0,'source_differences':differences,
            'enhanced_only_source':removed,'original_commented_out_in_reference':commented,'instructions':instructions})
        if name in ('updateGameMessageDisplay','setBackgroundColor','setupBitplaneMasks'):
            routines[-1]['native_scope']='Supplemental graphics-boundary inspection; not executed by the standalone timing oracle or implemented natively here.'
        listing.append('; '+name+' SHA256 '+sha(data));listing.extend(f'{i.address:#x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}' for i in decoded);listing.append('')
    for name,start,end in [('primary-font renderer',0x594c6,0x59806),('masked-object renderer',0x69f42,0x69fe8)]:
        data=payload[start-BASE:end-BASE];require(chip[start:end]==data,name+' capture differs')
        routines.append({'name':name,'runtime_address':hex(start),'end_exclusive':hex(end),'size':len(data),'sha256':sha(data),
            'capture_equals_original':True,'native_scope':'Original executes in oracle; bitmap/text formatting outputs excluded.'})
    start,end=0x5e46c,0x5e474;data=payload[start-BASE:end-BASE];require(chip[start:end]==data,'HUD positions table changed')
    tables=[{'name':'lap HUD positions','runtime_address':hex(start),'end_exclusive':hex(end),'size':len(data),'sha256':sha(data),'capture_equals_original':True}]
    report={'source_adf_sha256':ADF_SHA,'payload_sha256':PAYLOAD_SHA,'reference_commit':COMMIT,'reference_source_sha256':SOURCE_SHA,
            'reference_url':item['url'],'routines':routines,'tables':tables,
            'capture':{'directory':str(args.capture.resolve()),'ram_sha256':sha(chip),'source_sha256':capture['source_sha256']},
            'native_scope':'Full local scheduler and bookkeeping; renderer calls become ordered events. Network branch unsupported.',
            'entry_condition':'D1/D2 bits8..15 zero; currentPlayerId0..11; original oracle requires valid primary-font/framebuffer context.',
            'mutable_tables':{'times':'24 slots, minutes0x1c908 seconds0x1c920 subseconds0x1c938','record_names':'12 bytes at0x5ec48 and0x5ec55','player_names':'input table0x1ecab'},
            'surrounding_message_renderer':{
                'entry':'0x5e508','end_exclusive':'0x5e65a',
                'executes_when':'messageActive sign set, pauseState sign clear, and (blink==0 or signed blink>=3 or messageIndex==0x3c)',
                'message_gameplay_state':'Does not write messageActive, messageMode, messageIndex or RNG.',
                'final_direct_writes_if_executed':{'0x1bb48':'segmentRepeatCounter=0','0x1bb90':'renderModeFlag=0',
                    '0x1bbd3':'textTransparencyMode=0','0x1bbbc':'singleBufferRenderMode=0',
                    '0x1bb6d':'textHorizontalOffset=0','0x1bb6e':'textYOffset=0',
                    '0x59836..0x5983d':'background masks=0 via0x59806','0x66475':'render command color=2 via0x6770a'},
                'other_outputs':'Text cursor/character scratch and framebuffer pixels through original0x594c6.',
                'scope':'Read-only source/Capstone inspection; no native message renderer claimed.'},
            'quirks':['Original lap clock is not gated by frameThrottleFlag.','Negative raceCompletionCheckFlag calls blink timer twice.',
                      'checkForTieCondition returns sign of wrapped subtraction, not signed relational comparison.',
                      'Lap correction helper uses deterministic13*complement(progress lowbyte), no RNG.',
                      'Minute clamp uses signedCMP.B/BGE; all packed-byte patterns preserved, including invalidBCD.',
                      'Clear/copy/add record operations preserve original order, including overlapping logical record slots.']}
    args.output.mkdir(parents=True,exist_ok=True);(args.output/'reference-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    (args.output/'original-disassembly.asm').write_text('\n'.join(listing))
    print(json.dumps({'report':str(args.output/'reference-comparison.json'),'instruction_count':sum(r.get('instruction_count',0) for r in routines),'operand_wildcards':0,'source_operand_differences':len(changes)},indent=2))
if __name__=='__main__':main()
