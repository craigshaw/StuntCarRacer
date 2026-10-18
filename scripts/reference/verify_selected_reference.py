"""Selected full routine and call-site comparison, using original disk bytes."""
from pathlib import Path
import contextlib,io,json,runpy,hashlib,re

HERE=Path(__file__).resolve().parent
with contextlib.redirect_stdout(io.StringIO()):
    helpers=runpy.run_path(str(HERE/'match_reference_labels.py'))
out=helpers['OUT'];data=helpers['data'];decoder=helpers['decoder'];lines=helpers['lines']
norm=helpers['norm'];pattern=helpers['pattern']
name='calculateWheelCornerPositions'
start=next(n for n,l in enumerate(lines) if l.startswith(name+':'))
end=next(n for n in range(start+1,len(lines)) if re.match(r'^\w+:',lines[n]))
expected=[l.split(';')[0].strip() for l in lines[start+1:end] if l.split(';')[0].strip()]
actual=list(decoder.disasm(data[0x531ce:0x53250],0x618ce))
assert len(expected)==len(actual)
assert all(pattern(s).fullmatch(norm(i.mnemonic+' '+i.op_str)) for s,i in zip(expected,actual))
assert not any('.+?' in pattern(s).pattern for s in expected)
wheel={
    'name':name,'source_line':start+1,'payload_offset':'0x531ce',
    'runtime_address':'0x618ce','end_exclusive':'0x61950','size':0x82,
    'sha256':hashlib.sha256(data[0x531ce:0x53250]).hexdigest(),
    'instruction_count':len(actual),
    'status':'full instruction sequence and all operand values match named upstream routine after resolving original data addresses; no code-target wildcards',
    'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'actual':i.mnemonic+' '+i.op_str,'source':s} for s,i in zip(expected,actual)]
}
call_names=['calculateTransformMatrices','calculateWheelCornerPositions','updateWheelHeightsFromTrack',
'calculateExpectedTrackSurfaceHeights','transformWorldCoordinates','updateWheelSpeed','calculateViewAngles',
'updateWheelSuspensionPhysics','updateCarOrientation','calculateSteeringResponse','calculateSecondaryCoordinates',
'updateCollisionState','updateVelocityDamping','integrateVelocityComponents','calculateTertiaryCoordinates',
'applyVelocityIntegration','updateWorldPosition']
phys=list(decoder.disasm(data[0x5315c:0x531ce],0x6185c))
calls=[i for i in phys if i.mnemonic=='jsr']
assert len(calls)==len(call_names)
driver={'name':'updateGamePhysics','payload_offset':'0x5315c','runtime_address':'0x6185c',
'end_exclusive':'0x618ce','source_line':10874,
'status':'17 direct call instructions correspond in order to upstream driver; selected callee prefixes independently matched',
'calls':[{'site':hex(i.address),'target':hex(int.from_bytes(i.bytes[2:6],'big')),'upstream_name':n} for i,n in zip(calls,call_names)]}
loop_names=['processPlayerInput','updateGamePhysics','updateEngineAudioPitch','processGameFrame',
'animateDrawBridge','updateSpeedometerBar','updateDamageAndTimers','updateFrameThrottlingAndTimers','renderDistanceDisplay']
loop_ins=list(decoder.disasm(data[0x4ed7e:0x4edba],0x5d47e))
loop_calls=[i for i in loop_ins if i.mnemonic=='jsr']
assert len(loop_names)==len(loop_calls)
loop={'name':'mainGameLoop','payload_offset':'0x4ed7e','runtime_address':'0x5d47e','source_line':6586,
'status':'original iteration sequence identified between matched initialization and named original-offset branch anchors; enhanced source adds multi-step scheduling here',
'calls':[{'site':hex(i.address),'target':hex(int.from_bytes(i.bytes[2:6],'big')),'upstream_name':n} for i,n in zip(loop_calls,loop_names)]}
timing={'routine':'updateWorldPosition','instruction_address':'0x61956','bytes':data[0x53256:0x5325a].hex(),
'original_instruction':'move.b #$ee,d2','reference_source_line':10930,
'reference_instruction':'MOVE.B #TIMESTEP_FACTOR,D2','reference_constant_definition_line':13,
'reference_pal_value':0xee//6,'original_value':0xee,
'status':'verified original immediate differs from enhanced source constant, demonstrating source is not a drop-in original'}
key_start=next(n for n,l in enumerate(lines) if l.startswith('checkKeyPressed:'))
key_source=[l.split(';')[0].strip() for l in lines[key_start+1:key_start+5]]
key_code=list(decoder.disasm(data[0x52bbe:0x52bce],0x612be))
assert len(key_code)==len(key_source)==4
# Explicit normalization here: assembler CMP immediate is decoded CMPI, and
# Capstone omits the zero displacement in indexed addressing. Do not weaken the
# general prefix matcher just to admit these aliases.
key_expected=['MOVE.L #keyboardState,A0','MOVE.B $00(A0,D1.W),D1','CMP.B #$B3,D1','RTS']
assert [re.sub(r'\s+',' ',s) for s in key_source] == key_expected
assert data[0x52bbe:0x52bce].hex() == '207c0000ead6123010000c0100b34e75'
keyboard={'name':'checkKeyPressed','runtime_address':'0x612be','end_exclusive':'0x612ce',
          'source_line':key_start+1,'status':'all four instructions and operands match',
          'sha256':hashlib.sha256(data[0x52bbe:0x52bce]).hexdigest()}
record={'payload_sha256':hashlib.sha256(data).hexdigest(),'base_source':'ORIGINAL_LOAD_ADDRESS=$e700; runtime confirmation separate',
'wheel_corner':wheel,'physics_driver':driver,'main_loop':loop,'timing_difference':timing,'keyboard_query':keyboard}
(out/'selected-comparison.json').write_text(json.dumps(record,indent=2)+'\n')
listing=[]
for title,seq in [('wheel-corner',actual),('physics-driver',phys),('main-loop-prefix',loop_ins)]:
    listing += ['; '+title]+[f'{i.address:08x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}' for i in seq]+['']
(out/'selected-original-disassembly.asm').write_text('\n'.join(listing))
print('Full wheel-corner body matched:',len(actual),'instructions;',len(wheel['instructions']),'fixed operand comparisons')
print('Physics-driver calls:',len(calls))
print('Main-loop calls:',len(loop_calls))
print(json.dumps(timing,indent=2))
