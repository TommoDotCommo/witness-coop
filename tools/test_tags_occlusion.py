"""Game-free checks that startup failures and missing pixel queries cannot count as mutant catches."""
import json
from qa_tags_occlusion import verdict
wall = 'a name tag behind the wall has zero visible pixels'
def result(visible, **changes):
    data = dict(checks={'open control': True, 'B behind wall': True, wall: visible == 0}, behind={'vis': visible, 'drawn': 1})
    data.update(changes)
    return 'TAG_OCC_RESULT ' + json.dumps(data)
assert verdict(0, result(0), True)
assert not verdict(1, result(1000), True)
assert verdict(1, result(1000), False)
assert not verdict(1, '', False)
assert not verdict(1, 'TAG_OCC_RESULT {}', False)
assert not verdict(1, result(0), False)
assert not verdict(1, result(1000, checks={'open control': False, wall: False}), False)
assert not verdict(1, result(1000, behind={'vis': 1000, 'drawn': 0}), False)
assert not verdict(1, result(0, checks={'open control': True, wall: False}), False)
assert not verdict(0, result(0, checks={}), True)
print('tag occlusion scorer: 10 checks passed')
