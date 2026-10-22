"""Find source instruction-prefix matches in supplied plaintext game image.

This is a conservative navigation aid, not a proof of whole-function equivalence.
Named upstream code operands are wildcards; known data addresses stay exact.
"""
from pathlib import Path
import json,re
from capstone import Cs,CS_ARCH_M68K,CS_MODE_BIG_ENDIAN,CS_MODE_M68K_000

OUT=Path(__file__).resolve().parents[3]/'analysis/reference/vesuri'
BASE=0xe700
data=(OUT/'offline-extracted-payload.bin').read_bytes()
lines=(OUT/'StuntCarRacer.s').read_text().splitlines()
syms={s['name'].lower():int(s['reference_runtime_address'],16) for s in json.loads((OUT/'data-symbols.json').read_text())}
for line in lines:
    m=re.match(r'^(\w+)\s+equ\s+\$([0-9a-fA-F]+)\s*(?:;.*)?$',line)
    if m:syms[m[1].lower()]=int(m[2],16)

def norm(s):
    s=s.lower().replace('a7','sp')
    s=re.sub(r'\$([0-9a-f]+)',lambda m:str(int(m[1],16)),s)
    s=re.sub(r'\b(move|add|sub|cmp)a\.',r'\1.',s)
    s=re.sub(r'\b(muls|mulu|divs|divu)\.w\b',r'\1',s)
    s=re.sub(r'\b(b[a-z]{1,3})\.[ws]\b',r'\1',s)
    s=re.sub(r'(\d)\.[wl]\b',r'\1',s)
    return re.sub(r'\s+','',s)

def pattern(instruction):
    parts=instruction.strip().split(None,1)
    mnemonic=parts[0]
    operand=parts[1] if len(parts)>1 else ''
    # Only expressions used for named addresses and simple immediate sums.
    def sub(m):
        word=m[0].lower()
        if word in syms:return str(syms[word])
        if re.fullmatch(r'(?:[ad][0-7]|sp|sr|ccr|pc|w|l|b)',word):return word
        if word.startswith('lbc') and re.fullmatch('lbc[0-9a-f]+',word):return str(BASE+int(word[3:],16))
        return 'WILDCARD'
    operand=re.sub(r'(?<![\w$])[A-Za-z_][\w]*(?![\w])',sub,operand)
    operand=re.sub(r'\$([0-9a-fA-F]+)',lambda m:str(int(m[1],16)),operand)
    operand=re.sub(r'(?<!\w)(\d+)\+(\d+)(?!\w)',lambda m:str(int(m[1])+int(m[2])),operand)
    p=re.escape(norm(mnemonic+' '+operand)).replace('wildcard',r'.+?')
    return re.compile('^'+p+'$')

decoder=Cs(CS_ARCH_M68K,CS_MODE_BIG_ENDIAN|CS_MODE_M68K_000)
ins={};idx={}
for off in range(0,len(data)-12,2):
    got=next(decoder.disasm(data[off:off+12],BASE+off,count=1),None)
    if got is None:continue
    text=norm(got.mnemonic+' '+got.op_str)
    ins[off]=(got.size,text,got.mnemonic+' '+got.op_str,got.bytes.hex())
    key=norm(got.mnemonic)
    idx.setdefault(key,[]).append(off)

found=[]
anchors=[(n,int(m[1],16)) for n,line in enumerate(lines[:20160])
         if (m:=re.match(r'^lbC([0-9A-Fa-f]+):',line))]
for n,line in enumerate(lines[:20160]):
    m=re.match(r'^([A-Za-z_]\w*):\s*(.*)$',line)
    if not m or re.match('lb[Cc]',m[1]):continue
    snippet=[]
    subsequent=[m[2]]+lines[n+1:n+22]
    for txt in subsequent:
        txt=txt.split(';')[0].strip()
        if not txt:continue
        if re.match(r'^\w+:',txt):break
        txt=re.sub(r'^\.[\w]+:\s*','',txt)
        if not txt:continue
        if txt.split()[0].lower() in ['ifnd','ifd','ifeq','ifne','else','endc','section','dc.b','dc.w','dc.l','ds.b','ds.w','ds.l']:break
        snippet.append(txt)
        if len(snippet)>=7:break
    if len(snippet)<4:continue
    pats=[pattern(s) for s in snippet]
    key=norm(snippet[0].split()[0])
    before=[a for ln,a in anchors if ln<n]
    after=[a for ln,a in anchors if ln>n]
    lo=before[-1] if before else 0
    hi=after[0] if after else len(data)
    matches=[]
    for off in idx.get(key,[]):
        if not lo<=off<=hi:continue
        cur=off;actual=[]
        for p in pats:
            value=ins.get(cur)
            if not value or not p.fullmatch(value[1]):break
            actual.append({'address':hex(BASE+cur),'instruction':value[2],'bytes':value[3]})
            cur+=value[0]
        else:matches.append({'payload_offset':hex(off),'runtime_address':hex(BASE+off),'instructions':actual})
    if len(matches)==1:
        found.append({'name':m[1],'source_line':n+1,'source_prefix':snippet,'match':matches[0],
          'source_anchor_bounds_offsets':[hex(lo),hex(hi)],
          'status':'unique instruction-prefix match between nearest surviving original-offset labels; code-target symbols wildcarded; not whole-function equivalence'})
(OUT/'matched-code-labels.json').write_text(json.dumps(found,indent=2)+'\n')
print('Unique matched source labels:',len(found))
for f in found:
    print(f["match"]["runtime_address"],f['name'])
