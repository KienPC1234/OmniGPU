#!/usr/bin/env python3
import json

d = json.load(open("gen/vulkan_api.json"))
names = [f["name"] for f in d["functions"] if f["name"].startswith("vkCreate")][:10]
print("First 10 vkCreate* in JSON:")
for n in names:
    print(f"  - {n}")
print(f"Total functions in JSON: {len(d['functions'])}")
names_set = {f["name"] for f in d["functions"]}
print(f"vkCreateInstance in JSON: {'vkCreateInstance' in names_set}")
print(f"vkDestroyInstance in JSON: {'vkDestroyInstance' in names_set}")
