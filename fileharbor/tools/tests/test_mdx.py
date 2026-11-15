# Run: scripts/test.sh   (or python3 fileharbor/tools/tests/test_mdx.py)   prints PASS/FAIL per check
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from fhtools import mdx


def check(name, got, want):
    ok = want in got if isinstance(want, str) else want(got)
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else "  got=%r" % (got,)))


SAMPLE = '''---
title: Hello
tags: [a, b]
---
import {Chart} from './chart'
export const meta = {
  a: 1
}

# Title {1 + 1}

<Callout type="warning">
Be **careful** with `{x}`.
</Callout>

Inline <Badge text="x" /> and <Em>word</Em> here.

<Chart data={[1,2]} onClick={() => go()} />

{/* hidden */}

```js
const a = {b: 1}; <Foo>
```
'''
body, head = mdx.convert(SAMPLE, True)
check("frontmatter table", head, "<th>title</th><td>Hello</td>")
check("frontmatter list", head, "a, b")
check("imports collapsed", head, "1 import/export statement")
check("import not in body", body, lambda b: "import {Chart}" not in b)
check("expression chip", body, '<code class="mdx-expr">{1 + 1}</code>')
check("callout box", body, 'mdx-kind-warning')
check("callout keeps markdown", body, "Be **careful**")
check("inline code braces untouched", body, "`{x}`")
check("self closing inline", body, "mdx-tag")
check("self closing block", body, 'class="mdx-component"')
check("comment removed", body, lambda b: "hidden" not in b)
check("code fence untouched", body, "const a = {b: 1}; <Foo>")
plain, phead = mdx.convert("---\nk: v\n---\n# T {x}\n", False)
check("plain md keeps braces", plain, "# T {x}")
check("plain md front matter", phead, "<th>k</th>")
check("no front matter", mdx.convert("# T\n", False)[1], lambda h: h == "")
