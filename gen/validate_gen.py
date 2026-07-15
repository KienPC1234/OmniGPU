#!/usr/bin/env python3
"""Validate the generated intercept file."""
import re

with open("src/guest/vk_intercept_gen.cpp") as f:
    content = f.read()

# Function DEFINITIONS have _hook suffix
def_hooks = set(re.findall(r"(?:VkResult|void|uint64_t)\s+VKAPI_PTR\s+(vk\w+?)_hook\(", content))
# Function REGISTRATIONS use base name
reg_hooks = set(re.findall(r'get_hook_fns\(\)\["(vk\w+)"\]', content))
# FunctionId references  
func_ids = set(re.findall(r"fbs::FunctionId_(\w+)", content))

print(f"Hook function definitions: {len(def_hooks)}")
print(f"Hook registrations: {len(reg_hooks)}")
print(f"FunctionId references: {len(func_ids)}")

# Verify all defs are registered
missing_reg = def_hooks - reg_hooks
if missing_reg:
    print(f"ERROR: {len(missing_reg)} hooks defined but NOT registered:")
    for h in sorted(missing_reg):
        print(f"  - {h}")
else:
    print("OK: All defined hooks are registered")

# Verify all registrations have FunctionId
missing_id = reg_hooks - func_ids
if missing_id:
    print(f"WARNING: {len(missing_id)} functions missing FunctionId:")
    for h in sorted(missing_id):
        print(f"  - {h}")
else:
    print("OK: All registered hooks have FunctionId")

# File structure check
print(f"File ends with '}}': {content.strip().endswith('}')}")
print(f"File size: {len(content)} bytes")
print(f"Lines: {content.count(chr(10))}")
