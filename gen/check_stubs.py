#!/usr/bin/env python3
import re

with open('src/host/command_dispatcher.cpp') as f:
    content = f.read()

func_ids = set(re.findall(r'REGISTER\(fbs::FunctionId_(\w+)', content))
stub_ids = set(re.findall(r'REGISTER\(fbs::FunctionId_(\w+).*?STUB', content, re.DOTALL))

print(f"Total registered handlers: {len(func_ids)}")
print(f"Stubs (STUB logged): {len(stub_ids)}")
print(f"Real implementations: {len(func_ids) - len(stub_ids)}")

print("\n=== STUB functions ===")
for s in sorted(stub_ids):
    print(f"  - {s}")

print("\n=== Real implementations ===")
real = func_ids - stub_ids
for r in sorted(real):
    print(f"  - {r}")
