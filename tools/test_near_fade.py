"""Game-free scorer checks: only a valid near-body failure catches the mutant."""
import json
from qa_near_fade import verdict, REQUIRED
near={'overlapping body invisible','overlapping tag invisible','close body invisible','close tag invisible'}
def result(mutant=False, **extra):
    checks={k:True for k in REQUIRED}
    if mutant:
        checks['overlapping body invisible']=False; checks['close body invisible']=False
    checks.update(extra)
    return 'NEAR_FADE_RESULT '+json.dumps(dict(checks=checks))
assert verdict(0,result(),True)
assert not verdict(1,result(True),True)
assert verdict(1,result(True),False)
assert not verdict(1,'',False)
assert not verdict(1,'NEAR_FADE_RESULT {}',False)
assert not verdict(1,result(),False)
assert not verdict(0,result(True),False)
assert not verdict(1,result(True,**{'far body and tag visible':False}),False)
assert not verdict(0,'NEAR_FADE_RESULT {"checks":{}}',True)
assert not verdict(1,'NEAR_FADE_RESULT {"checks":[]}',False)
missing=json.loads(result()[len('NEAR_FADE_RESULT '):]); del missing['checks']['far pose reached']
assert not verdict(0,'NEAR_FADE_RESULT '+json.dumps(missing),True)
print('near fade scorer: 11 checks passed')
