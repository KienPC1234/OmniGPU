#!/usr/bin/env python3
"""Fix the blendConstants type in vulkan_api.json"""
import json

with open("gen/vulkan_api.json") as f:
    d = json.load(f)

for f in d["functions"]:
    if f["name"] == "vkCmdSetBlendConstants":
        for p in f["params"]:
            if p["name"] == "blendConstants" and not p["type"].endswith("*"):
                p["type"] += "*"
                print(f"Fixed: {p['type']} {p['name']}")

with open("gen/vulkan_api.json", "w") as f:
    json.dump(d, f, indent=2)
