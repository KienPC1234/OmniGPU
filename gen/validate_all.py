#!/usr/bin/env python3
"""Cross-check: verify all function names in generated code match FunctionId entries in protocol."""

import re

# Read generated file
with open("src/guest/vk_intercept_gen.cpp") as f:
    gen = f.read()

# Read protocol schema
with open("src/schemas/omnigpu_protocol.fbs", encoding="utf-8") as f:
    fbs = f.read()

# Read manual hook registrations
with open("src/guest/vk_intercept.cpp") as f:
    manual = f.read()

# Read header declarations
with open("src/guest/vk_intercept.h") as f:
    header = f.read()

# Extract all function names from get_hook_fns registrations in generated code
gen_funcs = set(re.findall(r'get_hook_fns\(\)\["(\w+)"\]', gen))

# Extract all FunctionId enum names from protocol
proto_funcs = set(re.findall(r'^\s+(\w+)\s*=', fbs, re.MULTILINE))
proto_funcs.discard("NONE")

# Extract all manual registrations
manual_funcs = set(re.findall(r'register_manual_hook\("vk\w+"', manual))
manual_funcs = set(re.findall(r'register_manual_hook\("([^"]+)"', manual))

# Find issues
missing_proto = gen_funcs - proto_funcs
orphan_proto = proto_funcs - gen_funcs - {"NONE"} - manual_funcs

print(f"Generated functions: {len(gen_funcs)}")
print(f"Protocol FunctionId entries: {len(proto_funcs)}")
print(f"Manual hook registrations: {len(manual_funcs)}")

# Check manual hooks declared in header vs registered
header_manual = set(re.findall(r'VKAPI_PTR\s+(\w+)_hook\(', header))
manual_declared = {f for f in header_manual if f not in re.findall(r'DECL_HOOK\(', header)}
# Better: separate manual decls from auto-gen decls
# Manual hooks are those not using DECL_HOOK

print()
if missing_proto:
    print(f"ERROR: {len(missing_proto)} generated functions MISSING from FunctionId enum:")
    for f in sorted(missing_proto):
        print(f"  - {f}")
else:
    print("OK: All generated functions have FunctionId entries")

orphan_non_manual = orphan_proto - manual_funcs - set(re.findall(r'vk\w+_hook', '\n'.join(header_manual)))
# Actually just check the ones actually used
print()
if orphan_proto:
    print(f"INFO: {len(orphan_proto)} protocol entries not in generated code or manual (likely forward-looking):")
    for f in sorted(orphan_proto)[:20]:
        print(f"  - {f}")
    if len(orphan_proto) > 20:
        print(f"  ... and {len(orphan_proto)-20} more")

# Check for function name mismatches  
print()
print("--- Manual hooks declared in header vs registered in .cpp ---")
header_manual_hooks = set(re.findall(r'VKAPI_PTR\s+(\w+)_hook', header))
# Filter out auto-gen DECL_HOOK macros
header_decl_auto = set(re.findall(r'DECL_HOOK\([^,]+,\s*(\w+)', header))
header_manual_hooks -= {f"{h}_hook" for h in header_decl_auto}
header_manual_hooks -= header_decl_auto

# Check each manual hook in header is registered  
for h in sorted(header_manual_hooks):
    # Extract function name from hook name (remove _hook)
    func_name = h.replace("_hook", "")
    if func_name not in manual_funcs:
        print(f"  WARNING: Header declares {h} but it's NOT registered in ManualHookRegistrar")

print(f"\n  Header manual hook declarations: {len(header_manual_hooks)}")
print(f"  Manual registrations: {len(manual_funcs)}")
